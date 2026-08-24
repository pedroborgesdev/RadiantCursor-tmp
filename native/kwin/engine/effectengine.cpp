// SPDX-License-Identifier: GPL-3.0-or-later
#include "effectengine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace KWin::RadiantCursorEngine
{
namespace
{
constexpr int MaxJsonBytes = 256 * 1024;
constexpr int MaxLayers = 128;
constexpr int MaxKeyframes = 64;
constexpr int MaxDurationMs = 10000;
constexpr int MaxParticles = 256;

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float pseudoRandom(int index, int variant, int channel)
{
    quint32 value = quint32(index + 1) * 0x9e3779b9U;
    value ^= quint32(variant + 11) * 0x85ebca6bU;
    value ^= quint32(channel + 23) * 0xc2b2ae35U;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return float(value & 0xffffU) / 65535.0f;
}

Easing easingFromString(const QString &name)
{
    if (name == QLatin1String("easeInQuad")) return Easing::EaseInQuad;
    if (name == QLatin1String("easeOutQuad")) return Easing::EaseOutQuad;
    if (name == QLatin1String("easeInOutQuad")) return Easing::EaseInOutQuad;
    if (name == QLatin1String("easeInCubic")) return Easing::EaseInCubic;
    if (name == QLatin1String("easeOutCubic")) return Easing::EaseOutCubic;
    if (name == QLatin1String("easeInOutCubic")) return Easing::EaseInOutCubic;
    if (name == QLatin1String("easeOutBack")) return Easing::EaseOutBack;
    if (name == QLatin1String("easeOutBounce")) return Easing::EaseOutBounce;
    if (name == QLatin1String("easeOutElastic")) return Easing::EaseOutElastic;
    return Easing::Linear;
}

QVector2D parseVec2(const QJsonValue &value, const QVector2D &fallback = {})
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble()) return fallback;
    return QVector2D(float(array[0].toDouble()), float(array[1].toDouble()));
}

template<typename T, typename Parser>
Track<T> parseTrack(const QJsonValue &value, const T &fallback, Parser parser)
{
    Track<T> track;
    track.constant = fallback;
    if (!value.isObject() || !value.toObject().contains(QStringLiteral("keyframes"))) {
        track.constant = parser(value, fallback);
        return track;
    }
    const QJsonArray frames = value.toObject().value(QStringLiteral("keyframes")).toArray();
    if (frames.isEmpty() || frames.size() > MaxKeyframes) return track;
    track.animated = true;
    track.frames.reserve(frames.size());
    float previous = -1.0f;
    for (const QJsonValue &entry : frames) {
        const QJsonObject object = entry.toObject();
        const float time = float(object.value(QStringLiteral("time")).toDouble(-1.0));
        if (time < previous || time < 0.0f || time > 1.0f) {
            track.animated = false;
            track.frames.clear();
            return track;
        }
        track.frames.push_back(Keyframe<T>{time, parser(object.value(QStringLiteral("value")), fallback), easingFromString(object.value(QStringLiteral("easing")).toString())});
        previous = time;
    }
    return track;
}

Track<float> floatTrack(const QJsonValue &value, float fallback)
{
    return parseTrack<float>(value, fallback, [](const QJsonValue &entry, float defaultValue) {
        return entry.isDouble() ? float(entry.toDouble()) : defaultValue;
    });
}

Track<QVector2D> vectorTrack(const QJsonValue &value, const QVector2D &fallback)
{
    return parseTrack<QVector2D>(value, fallback, [](const QJsonValue &entry, const QVector2D &defaultValue) {
        return parseVec2(entry, defaultValue);
    });
}

template<typename T, typename Interpolator>
T evaluateTrack(const Track<T> &track, float progress, Interpolator interpolate)
{
    if (!track.animated || track.frames.isEmpty()) return track.constant;
    if (progress <= track.frames.front().time) return track.frames.front().value;
    for (int index = 1; index < track.frames.size(); ++index) {
        const Keyframe<T> &next = track.frames[index];
        const Keyframe<T> &previous = track.frames[index - 1];
        if (progress <= next.time) {
            const float amount = evaluateEasing(previous.easing, (progress - previous.time) / std::max(0.000001f, next.time - previous.time));
            return interpolate(previous.value, next.value, amount);
        }
    }
    return track.frames.back().value;
}

float evaluateFloat(const Track<float> &track, float progress)
{
    return evaluateTrack<float>(track, progress, [](float from, float to, float amount) { return std::lerp(from, to, amount); });
}

QVector2D evaluateVector(const Track<QVector2D> &track, float progress)
{
    return evaluateTrack<QVector2D>(track, progress, [](const QVector2D &from, const QVector2D &to, float amount) { return from + (to - from) * amount; });
}

QColor parseColor(const QJsonValue &value, bool &usesButton, const QColor &fallback)
{
    usesButton = false;
    if (value.isObject() && value.toObject().value(QStringLiteral("ref")).toString().startsWith(QLatin1String("click."))) {
        usesButton = true;
        return fallback;
    }
    const QColor parsed(value.toString());
    return parsed.isValid() ? parsed : fallback;
}

CompiledGeometry parseGeometry(const QJsonObject &object)
{
    CompiledGeometry geometry;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("ring")) geometry.kind = GeometryKind::Ring;
    else if (kind == QLatin1String("rect")) geometry.kind = GeometryKind::Rect;
    else if (kind == QLatin1String("line")) geometry.kind = GeometryKind::Line;
    else if (kind == QLatin1String("polygon")) geometry.kind = GeometryKind::Polygon;
    else if (kind == QLatin1String("star")) geometry.kind = GeometryKind::Star;
    else if (kind == QLatin1String("diamond")) geometry.kind = GeometryKind::Diamond;
    else geometry.kind = GeometryKind::Circle;
    geometry.radius = float(object.value(QStringLiteral("radius")).toDouble(20.0));
    geometry.size = parseVec2(object.value(QStringLiteral("size")), QVector2D(40.0f, 40.0f));
    geometry.lineStart = parseVec2(object.value(QStringLiteral("start")));
    geometry.lineEnd = parseVec2(object.value(QStringLiteral("end")));
    geometry.innerRadius = float(object.value(QStringLiteral("innerRadius")).toDouble(8.0));
    geometry.points = std::clamp(object.value(QStringLiteral("points")).toInt(object.value(QStringLiteral("sides")).toInt(5)), 2, 64);
    return geometry;
}

CompiledMaterial parseMaterial(const QJsonObject &object)
{
    CompiledMaterial material;
    const QJsonValue fill = object.value(QStringLiteral("fill"));
    material.hasFill = fill.isObject();
    if (material.hasFill) material.fillColor = parseColor(fill.toObject().value(QStringLiteral("color")), material.fillUsesButtonColor, Qt::white);
    const QJsonValue stroke = object.value(QStringLiteral("stroke"));
    material.hasStroke = stroke.isObject();
    if (material.hasStroke) {
        material.strokeColor = parseColor(stroke.toObject().value(QStringLiteral("color")), material.strokeUsesButtonColor, Qt::white);
        material.strokeWidth = floatTrack(stroke.toObject().value(QStringLiteral("width")), 2.0f);
    }
    material.opacity = floatTrack(object.value(QStringLiteral("opacity")), 1.0f);
    material.glow = float(object.value(QStringLiteral("glow")).toDouble(0.0));
    material.blendMode = object.value(QStringLiteral("blendMode")).toString() == QLatin1String("additive") ? BlendMode::Additive : BlendMode::Normal;
    return material;
}

void appendPolygon(RenderCommand &command, const CompiledGeometry &geometry, const QPointF &center,
                   const QVector2D &scale, float rotation)
{
    int vertices = geometry.points;
    bool star = geometry.kind == GeometryKind::Star;
    if (geometry.kind == GeometryKind::Rect || geometry.kind == GeometryKind::Diamond) vertices = 4;
    if (star) vertices *= 2;
    command.points.reserve(vertices);
    const float offset = geometry.kind == GeometryKind::Diamond ? std::numbers::pi_v<float> / 4.0f : -std::numbers::pi_v<float> / 2.0f;
    for (int index = 0; index < vertices; ++index) {
        const float angle = offset + rotation + 2.0f * std::numbers::pi_v<float> * index / vertices;
        float radius = geometry.radius;
        if (geometry.kind == GeometryKind::Rect || geometry.kind == GeometryKind::Diamond) {
            const float x = (index == 0 || index == 3) ? -geometry.size.x() * 0.5f : geometry.size.x() * 0.5f;
            const float y = index < 2 ? -geometry.size.y() * 0.5f : geometry.size.y() * 0.5f;
            const float cosine = std::cos(rotation);
            const float sine = std::sin(rotation);
            command.points << QVector2D(center) + QVector2D((x * cosine - y * sine) * scale.x(), (x * sine + y * cosine) * scale.y());
            continue;
        }
        if (star && index % 2 == 1) radius = geometry.innerRadius;
        command.points << QVector2D(center) + QVector2D(std::cos(angle) * radius * scale.x(), std::sin(angle) * radius * scale.y());
    }
}

void appendGeometry(const CompiledGeometry &geometry, const CompiledMaterial &material,
                    const QPointF &center, const QVector2D &scale, float rotation,
                    float progress, const QColor &buttonColor, QVector<RenderCommand> &commands)
{
    const float opacity = clamp01(evaluateFloat(material.opacity, progress));
    auto append = [&](bool filled, QColor color, float width) {
        color.setAlphaF(color.alphaF() * opacity);
        if (color.alpha() == 0) return;
        RenderCommand command;
        command.center = center;
        command.width = width;
        command.filled = filled;
        command.color = color;
        command.blendMode = material.blendMode;
        if (geometry.kind == GeometryKind::Circle || geometry.kind == GeometryKind::Ring) {
            command.kind = RenderCommand::Kind::Circle;
            command.radius = geometry.radius * std::max(std::abs(scale.x()), std::abs(scale.y()));
            command.filled = filled && geometry.kind != GeometryKind::Ring;
        } else if (geometry.kind == GeometryKind::Line) {
            command.kind = RenderCommand::Kind::Lines;
            const float cosine = std::cos(rotation);
            const float sine = std::sin(rotation);
            auto transformed = [&](const QVector2D &point) {
                return QVector2D(center) + QVector2D((point.x() * cosine - point.y() * sine) * scale.x(), (point.x() * sine + point.y() * cosine) * scale.y());
            };
            command.points << transformed(geometry.lineStart) << transformed(geometry.lineEnd);
        } else {
            command.kind = RenderCommand::Kind::Polygon;
            appendPolygon(command, geometry, center, scale, rotation);
        }
        commands.push_back(std::move(command));
    };
    if (material.hasFill) append(true, material.fillUsesButtonColor ? buttonColor : material.fillColor, 1.0f);
    if (material.hasStroke) append(false, material.strokeUsesButtonColor ? buttonColor : material.strokeColor, std::max(0.5f, evaluateFloat(material.strokeWidth, progress)));
}
}

float evaluateEasing(Easing easing, float progress)
{
    const float x = clamp01(progress);
    switch (easing) {
    case Easing::EaseInQuad: return x * x;
    case Easing::EaseOutQuad: return 1.0f - (1.0f - x) * (1.0f - x);
    case Easing::EaseInOutQuad: return x < 0.5f ? 2.0f * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 2.0f) / 2.0f;
    case Easing::EaseInCubic: return x * x * x;
    case Easing::EaseOutCubic: return 1.0f - std::pow(1.0f - x, 3.0f);
    case Easing::EaseInOutCubic: return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
    case Easing::EaseOutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
    }
    case Easing::EaseOutBounce: {
        constexpr float n = 7.5625f;
        constexpr float d = 2.75f;
        if (x < 1.0f / d) return n * x * x;
        if (x < 2.0f / d) { const float y = x - 1.5f / d; return n * y * y + 0.75f; }
        if (x < 2.5f / d) { const float y = x - 2.25f / d; return n * y * y + 0.9375f; }
        const float y = x - 2.625f / d; return n * y * y + 0.984375f;
    }
    case Easing::EaseOutElastic:
        return x == 0.0f || x == 1.0f ? x : std::pow(2.0f, -10.0f * x) * std::sin((x * 10.0f - 0.75f) * 2.0f * std::numbers::pi_v<float> / 3.0f) + 1.0f;
    default: return x;
    }
}

void CompiledEffect::evaluate(int elapsedMs, const QPointF &origin, const QColor &buttonColor,
                              int variant, QVector<RenderCommand> &commands) const
{
    if (!layers.isEmpty() && layers.front().motionNode) {
        struct WorldState { QVector2D position; QVector2D scale = QVector2D(1.0f, 1.0f); float rotation = 0.0f; float opacity = 1.0f; int globalStart = 0; bool active = true; };
        QVector<WorldState> states(layers.size());
        auto channelValue = [](const CompiledLayer::MotionChannel &channel, float time) {
            if (channel.frames.isEmpty()) return channel.multiply ? 1.0f : 0.0f;
            if (time <= channel.frames.front().time) return channel.frames.front().value;
            for (int i = 1; i < channel.frames.size(); ++i) {
                const auto &a = channel.frames[i - 1]; const auto &b = channel.frames[i];
                if (time <= b.time) { const float p = evaluateEasing(a.easing, (time - a.time) / std::max(0.001f, b.time - a.time)); return std::lerp(a.value, b.value, p); }
            }
            return channel.frames.back().value;
        };
        for (int index = 0; index < layers.size(); ++index) {
            const CompiledLayer &layer = layers[index]; WorldState state;
            const WorldState *parent = layer.parentIndex >= 0 && layer.parentIndex < index ? &states[layer.parentIndex] : nullptr;
            state.globalStart = (parent ? parent->globalStart : 0) + layer.startMs;
            const float localTime = float(elapsedMs - state.globalStart);
            state.active = layer.enabled && (!parent || parent->active) && localTime >= 0.0f && localTime <= layer.durationMs;
            QVector2D localPosition = layer.transform.position.constant;
            QVector2D localScale = layer.transform.scale.constant;
            float localRotation = layer.transform.rotationDegrees.constant;
            float localOpacity = layer.baseOpacity;
            for (const auto &channel : layer.motionChannels) {
                const float value = channelValue(channel, localTime);
                switch (channel.property) {
                case CompiledLayer::MotionProperty::PositionX: localPosition.setX(localPosition.x() + value); break;
                case CompiledLayer::MotionProperty::PositionY: localPosition.setY(localPosition.y() + value); break;
                case CompiledLayer::MotionProperty::Rotation: localRotation += value; break;
                case CompiledLayer::MotionProperty::ScaleX: localScale.setX(localScale.x() * value); break;
                case CompiledLayer::MotionProperty::ScaleY: localScale.setY(localScale.y() * value); break;
                case CompiledLayer::MotionProperty::Opacity: localOpacity *= value; break;
                }
            }
            if (parent) {
                const float angle = parent->rotation * std::numbers::pi_v<float> / 180.0f;
                const QVector2D scaled(localPosition.x() * parent->scale.x(), localPosition.y() * parent->scale.y());
                state.position = parent->position + QVector2D(scaled.x() * std::cos(angle) - scaled.y() * std::sin(angle), scaled.x() * std::sin(angle) + scaled.y() * std::cos(angle));
                state.scale = QVector2D(parent->scale.x() * localScale.x(), parent->scale.y() * localScale.y());
                state.rotation = parent->rotation + localRotation; state.opacity = parent->opacity * localOpacity;
            } else { state.position = localPosition; state.scale = localScale; state.rotation = localRotation; state.opacity = localOpacity; }
            states[index] = state;
            if (!state.active || layer.groupNode) continue;
            CompiledMaterial material = layer.material; material.opacity.constant *= state.opacity;
            appendGeometry(layer.geometry, material, origin + state.position.toPointF(), state.scale, state.rotation * std::numbers::pi_v<float> / 180.0f, 0.0f, buttonColor, commands);
        }
        return;
    }
    for (const CompiledLayer &layer : layers) {
        if (!layer.enabled || elapsedMs < layer.startMs || elapsedMs > layer.startMs + layer.durationMs) continue;
        const float progress = clamp01(float(elapsedMs - layer.startMs) / float(std::max(1, layer.durationMs)));
        const QVector2D offset = evaluateVector(layer.transform.position, progress);
        const QVector2D scale = evaluateVector(layer.transform.scale, progress);
        const float rotation = evaluateFloat(layer.transform.rotationDegrees, progress) * std::numbers::pi_v<float> / 180.0f;
        const QPointF center = origin + offset.toPointF();
        if (layer.kind == LayerKind::Shape) {
            appendGeometry(layer.geometry, layer.material, center, scale, rotation, progress, buttonColor, commands);
            continue;
        }
        if (layer.kind == LayerKind::Image && layer.image) {
            RenderCommand command;
            command.kind = RenderCommand::Kind::Image;
            command.center = center;
            command.image = layer.image;
            command.imageKey = layer.imageKey;
            command.imageSize = QSizeF(layer.imageSize.x() * std::abs(scale.x()), layer.imageSize.y() * std::abs(scale.y()));
            command.rotationRadians = rotation;
            command.color = QColor(Qt::white);
            command.color.setAlphaF(clamp01(evaluateFloat(layer.material.opacity, progress)));
            command.blendMode = layer.material.blendMode;
            commands.push_back(std::move(command));
            continue;
        }
        if (layer.kind != LayerKind::Particles) continue;
        const float seconds = float(elapsedMs - layer.startMs) / 1000.0f;
        const float damping = std::pow(std::max(0.001f, 1.0f - layer.emitter.drag), seconds * 10.0f);
        for (int index = 0; index < layer.emitter.count; ++index) {
            const float unit = layer.emitter.distribution == CompiledEmitter::Distribution::Even
                ? (float(index) + 0.5f) / float(layer.emitter.count)
                : pseudoRandom(index, variant, 0);
            const float spread = layer.emitter.spreadDegrees * std::numbers::pi_v<float> / 180.0f;
            const float base = layer.emitter.angleDegrees * std::numbers::pi_v<float> / 180.0f;
            const float angle = base - spread * 0.5f + spread * unit + float(variant) * 0.73f;
            const QVector2D direction(std::cos(angle), std::sin(angle));
            const float speed = std::max(0.0f, layer.emitter.speed + (pseudoRandom(index, variant, 1) * 2.0f - 1.0f) * layer.emitter.speedVariation);
            QVector2D particlePosition(center);
            particlePosition += direction * (layer.emitter.spawnRadius * pseudoRandom(index, variant, 2) + speed * seconds * damping);
            particlePosition += layer.emitter.gravity * (0.5f * seconds * seconds);
            const float scalar = evaluateFloat(layer.particleScale, progress);
            const float particleRotation = rotation + evaluateFloat(layer.particleRotation, progress) * std::numbers::pi_v<float> / 180.0f + pseudoRandom(index, variant, 3) * std::numbers::pi_v<float>;
            appendGeometry(layer.geometry, layer.material, particlePosition.toPointF(), scale * scalar, particleRotation, progress, buttonColor, commands);
        }
    }
}

LoadResult EffectLoader::load(const QString &effectId, const QString &revision)
{
    LoadResult result;
    static const QRegularExpression safeId(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$"));
    static const QRegularExpression safeRevision(QStringLiteral("^sha256:[a-f0-9]{64}$"));
    auto fail = [&](const QString &code, const QString &path, const QString &message) {
        result.diagnostics.push_back(Diagnostic{Diagnostic::Severity::Error, code, path, message});
    };
    if (!safeId.match(effectId).hasMatch() || !safeRevision.match(revision).hasMatch()) {
        fail(QStringLiteral("invalid_activation"), QStringLiteral("$"), QStringLiteral("ID ou revisão ativa inválida."));
        return result;
    }
    const QString revisionRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/radiantcursor-studio/library/effects/") + effectId
        + QStringLiteral("/revisions/") + revision.mid(7);
    QFile runtimeFile(revisionRoot + QStringLiteral("/runtime.json"));
    if (runtimeFile.exists()) {
        if (!runtimeFile.open(QIODevice::ReadOnly) || runtimeFile.size() <= 0 || runtimeFile.size() > MaxJsonBytes * 2) {
            fail(QStringLiteral("runtime_read_failed"), QStringLiteral("$"), QStringLiteral("Não foi possível ler runtime.json.")); return result;
        }
        QJsonParseError runtimeError; const QJsonDocument runtimeJson = QJsonDocument::fromJson(runtimeFile.readAll(), &runtimeError);
        if (runtimeError.error != QJsonParseError::NoError || !runtimeJson.isObject()) { fail(QStringLiteral("runtime_invalid_json"), QStringLiteral("$"), runtimeError.errorString()); return result; }
        const QJsonObject root = runtimeJson.object(); const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray(); const int duration = root.value(QStringLiteral("durationMs")).toInt();
        if (root.value(QStringLiteral("runtimeVersion")).toInt() != 1 || root.value(QStringLiteral("effectId")).toString() != effectId || root.value(QStringLiteral("revision")).toString() != revision) { fail(QStringLiteral("runtime_identity"), QStringLiteral("$"), QStringLiteral("Runtime, ID ou revisão incompatível.")); return result; }
        if (duration < 1 || duration > MaxDurationMs || nodes.isEmpty() || nodes.size() > MaxLayers) { fail(QStringLiteral("runtime_limits"), QStringLiteral("$.nodes"), QStringLiteral("Duração ou quantidade de nós fora dos limites.")); return result; }
        auto compiled = std::make_shared<CompiledEffect>(); compiled->id = effectId; compiled->revision = revision; compiled->durationMs = duration; compiled->maximumBounds = std::clamp(float(root.value(QStringLiteral("maxRadius")).toDouble(256.0)), 32.0f, 4096.0f); compiled->layers.reserve(nodes.size());
        for (int index = 0; index < nodes.size(); ++index) {
            const QJsonObject source = nodes[index].toObject(); CompiledLayer layer; layer.motionNode = true; layer.id = source.value(QStringLiteral("id")).toString(); layer.enabled = source.value(QStringLiteral("visible")).toBool(true); layer.groupNode = source.value(QStringLiteral("kind")).toString() == QLatin1String("group"); layer.parentIndex = source.value(QStringLiteral("parentIndex")).toInt(-1); layer.startMs = source.value(QStringLiteral("startMs")).toInt(); layer.durationMs = source.value(QStringLiteral("durationMs")).toInt(); layer.baseOpacity = std::clamp(float(source.value(QStringLiteral("opacity")).toDouble(1.0)), 0.0f, 1.0f);
            if (layer.id.isEmpty() || layer.parentIndex >= index || layer.parentIndex < -1 || layer.startMs < 0 || layer.durationMs < 1 || layer.durationMs > MaxDurationMs) { fail(QStringLiteral("runtime_node"), QStringLiteral("$.nodes[%1]").arg(index), QStringLiteral("Nó compilado inválido.")); return result; }
            const QJsonObject transform = source.value(QStringLiteral("transform")).toObject(); layer.transform.position.constant = parseVec2(transform.value(QStringLiteral("position"))); layer.transform.scale.constant = parseVec2(transform.value(QStringLiteral("scale")), QVector2D(1,1)); layer.transform.rotationDegrees.constant = float(transform.value(QStringLiteral("rotationDeg")).toDouble()); const QVector2D size = parseVec2(transform.value(QStringLiteral("size")), QVector2D(40,40));
            const QJsonArray channels = source.value(QStringLiteral("channels")).toArray();
            for (const QJsonValue &channelValue : channels) {
                const QJsonObject channelObject = channelValue.toObject(); CompiledLayer::MotionChannel channel; const QString property = channelObject.value(QStringLiteral("property")).toString();
                if (property == QLatin1String("position.y")) channel.property = CompiledLayer::MotionProperty::PositionY; else if (property == QLatin1String("rotation")) channel.property = CompiledLayer::MotionProperty::Rotation; else if (property == QLatin1String("scale.x")) channel.property = CompiledLayer::MotionProperty::ScaleX; else if (property == QLatin1String("scale.y")) channel.property = CompiledLayer::MotionProperty::ScaleY; else if (property == QLatin1String("opacity")) channel.property = CompiledLayer::MotionProperty::Opacity; else channel.property = CompiledLayer::MotionProperty::PositionX;
                channel.multiply = channelObject.value(QStringLiteral("composition")).toString() == QLatin1String("multiply"); const QJsonArray frames = channelObject.value(QStringLiteral("keyframes")).toArray(); if (frames.size() < 2 || frames.size() > MaxKeyframes) { fail(QStringLiteral("runtime_keyframes"), QStringLiteral("$.nodes[%1].channels").arg(index), QStringLiteral("Track inválida.")); return result; }
                float previous = -1.0f; for (const QJsonValue &frameValue : frames) { const QJsonObject frame = frameValue.toObject(); const float time = float(frame.value(QStringLiteral("time")).toDouble(-1)); if (time < previous || time < 0 || time > layer.durationMs) { fail(QStringLiteral("runtime_keyframe_time"), QStringLiteral("$.nodes[%1].channels").arg(index), QStringLiteral("Tempo de keyframe inválido.")); return result; } channel.frames.push_back({time, float(frame.value(QStringLiteral("value")).toDouble()), easingFromString(frame.value(QStringLiteral("easing")).toString())}); previous = time; } layer.motionChannels.push_back(std::move(channel));
            }
            if (!layer.groupNode) {
                layer.kind = LayerKind::Shape; const QJsonObject shape = source.value(QStringLiteral("shape")).toObject(); const QString kind = shape.value(QStringLiteral("kind")).toString(); layer.geometry.size = size; layer.geometry.radius = std::min(size.x(), size.y()) * 0.5f;
                if (kind == QLatin1String("rectangle")) layer.geometry.kind = GeometryKind::Rect; else if (kind == QLatin1String("line")) { layer.geometry.kind = GeometryKind::Line; layer.geometry.lineStart = QVector2D(-size.x()*.5f,0); layer.geometry.lineEnd = QVector2D(size.x()*.5f,0); } else if (kind == QLatin1String("diamond")) layer.geometry.kind = GeometryKind::Diamond; else if (kind == QLatin1String("star")) { layer.geometry.kind = GeometryKind::Star; layer.geometry.points = std::clamp(shape.value(QStringLiteral("points")).toInt(5),2,32); layer.geometry.innerRadius = layer.geometry.radius * std::clamp(float(shape.value(QStringLiteral("innerRatio")).toDouble(.45)),.05f,.95f); } else if (kind == QLatin1String("triangle")) { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = 3; } else if (kind == QLatin1String("hexagon")) { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = 6; } else if (kind == QLatin1String("polygon")) { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = std::clamp(shape.value(QStringLiteral("sides")).toInt(5),3,32); } else layer.geometry.kind = GeometryKind::Circle;
                const QJsonObject appearance = source.value(QStringLiteral("appearance")).toObject(), fill = appearance.value(QStringLiteral("fill")).toObject(), stroke = appearance.value(QStringLiteral("stroke")).toObject(); layer.material.hasFill = fill.value(QStringLiteral("enabled")).toBool(); layer.material.fillColor = QColor(fill.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"))); layer.material.hasStroke = stroke.value(QStringLiteral("enabled")).toBool(); layer.material.strokeColor = QColor(stroke.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff"))); layer.material.strokeWidth.constant = std::max(.5f, float(stroke.value(QStringLiteral("width")).toDouble(2))); layer.material.opacity.constant = std::clamp(float(appearance.value(QStringLiteral("opacity")).toDouble(1)),0.0f,1.0f); layer.material.blendMode = appearance.value(QStringLiteral("blendMode")).toString() == QLatin1String("additive") ? BlendMode::Additive : BlendMode::Normal;
            }
            compiled->layers.push_back(std::move(layer));
        }
        result.effect = std::move(compiled); return result;
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/radiantcursor-studio/library/effects/") + effectId
        + QStringLiteral("/revisions/") + revision.mid(7) + QStringLiteral("/effect.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxJsonBytes) {
        fail(QStringLiteral("read_failed"), QStringLiteral("$"), QStringLiteral("Não foi possível ler a revisão declarativa."));
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        fail(QStringLiteral("invalid_json"), QStringLiteral("$"), parseError.errorString());
        return result;
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1 || root.value(QStringLiteral("id")).toString() != effectId
        || root.value(QStringLiteral("revision")).toString() != revision) {
        fail(QStringLiteral("identity_mismatch"), QStringLiteral("$"), QStringLiteral("Schema, ID ou hash não corresponde à ativação."));
        return result;
    }
    const int duration = root.value(QStringLiteral("durationMs")).toInt();
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (duration < 1 || duration > MaxDurationMs || layers.isEmpty() || layers.size() > MaxLayers) {
        fail(QStringLiteral("limits_exceeded"), QStringLiteral("$.layers"), QStringLiteral("Duração ou quantidade de layers fora dos limites."));
        return result;
    }
    auto compiled = std::make_shared<CompiledEffect>();
    compiled->id = effectId;
    compiled->revision = revision;
    compiled->durationMs = duration;
    compiled->layers.reserve(layers.size());
    for (int index = 0; index < layers.size(); ++index) {
        const QJsonObject source = layers[index].toObject();
        CompiledLayer layer;
        layer.id = source.value(QStringLiteral("id")).toString();
        layer.enabled = source.value(QStringLiteral("enabled")).toBool(true);
        const QJsonObject timing = source.value(QStringLiteral("timing")).toObject();
        layer.startMs = timing.value(QStringLiteral("startMs")).toInt();
        layer.durationMs = timing.value(QStringLiteral("durationMs")).toInt();
        if (layer.id.isEmpty() || layer.startMs < 0 || layer.durationMs < 1 || layer.durationMs > MaxDurationMs) {
            fail(QStringLiteral("invalid_layer"), QStringLiteral("$.layers[%1]").arg(index), QStringLiteral("Layer inválida."));
            return result;
        }
        const QJsonObject transform = source.value(QStringLiteral("transform")).toObject();
        layer.transform.position = vectorTrack(transform.value(QStringLiteral("position")), QVector2D());
        layer.transform.scale = vectorTrack(transform.value(QStringLiteral("scale")), QVector2D(1.0f, 1.0f));
        layer.transform.rotationDegrees = floatTrack(transform.value(QStringLiteral("rotationDeg")), 0.0f);
        const QString type = source.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("particles")) {
            layer.kind = LayerKind::Particles;
            const QJsonObject emitter = source.value(QStringLiteral("emitter")).toObject();
            layer.emitter.count = emitter.value(QStringLiteral("count")).toInt();
            if (layer.emitter.count < 1 || layer.emitter.count > MaxParticles) {
                fail(QStringLiteral("particle_limit"), QStringLiteral("$.layers[%1].emitter.count").arg(index), QStringLiteral("Quantidade de partículas inválida."));
                return result;
            }
            const QString mode = emitter.value(QStringLiteral("mode")).toString();
            layer.emitter.mode = mode == QLatin1String("cone") ? CompiledEmitter::Mode::Cone : mode == QLatin1String("point") ? CompiledEmitter::Mode::Point : CompiledEmitter::Mode::Radial;
            layer.emitter.distribution = emitter.value(QStringLiteral("distribution")).toString() == QLatin1String("even") ? CompiledEmitter::Distribution::Even : CompiledEmitter::Distribution::Random;
            layer.emitter.angleDegrees = float(emitter.value(QStringLiteral("angleDeg")).toDouble());
            layer.emitter.spreadDegrees = float(emitter.value(QStringLiteral("spreadDeg")).toDouble(360.0));
            layer.emitter.speed = float(emitter.value(QStringLiteral("speed")).toDouble());
            layer.emitter.speedVariation = float(emitter.value(QStringLiteral("speedVariation")).toDouble());
            layer.emitter.spawnRadius = float(emitter.value(QStringLiteral("spawnRadius")).toDouble());
            layer.emitter.gravity = parseVec2(emitter.value(QStringLiteral("gravity")));
            layer.emitter.drag = std::clamp(float(emitter.value(QStringLiteral("drag")).toDouble()), 0.0f, 1.0f);
            const QJsonObject particle = source.value(QStringLiteral("particle")).toObject();
            layer.geometry = parseGeometry(particle.value(QStringLiteral("geometry")).toObject());
            layer.material = parseMaterial(particle.value(QStringLiteral("material")).toObject());
            layer.particleScale = floatTrack(particle.value(QStringLiteral("scale")), 1.0f);
            layer.particleRotation = floatTrack(particle.value(QStringLiteral("rotationDeg")), 0.0f);
        } else if (type == QLatin1String("shape")) {
            layer.kind = LayerKind::Shape;
            layer.geometry = parseGeometry(source.value(QStringLiteral("geometry")).toObject());
            layer.material = parseMaterial(source.value(QStringLiteral("material")).toObject());
        } else if (type == QLatin1String("image")) {
            layer.kind = LayerKind::Image;
            layer.material = parseMaterial(source.value(QStringLiteral("material")).toObject());
            layer.imageSize = parseVec2(source.value(QStringLiteral("size")), QVector2D(64.0f, 64.0f));
            const QJsonObject asset = source.value(QStringLiteral("asset")).toObject();
            const QString assetId = asset.value(QStringLiteral("assetId")).toString();
            const QString mediaType = asset.value(QStringLiteral("mediaType")).toString();
            const QString extension = mediaType == QLatin1String("image/jpeg") ? QStringLiteral("jpg")
                : mediaType == QLatin1String("image/webp") ? QStringLiteral("webp") : QStringLiteral("png");
            if (!QRegularExpression(QStringLiteral("^sha256:[a-f0-9]{64}$")).match(assetId).hasMatch()) {
                fail(QStringLiteral("invalid_asset"), QStringLiteral("$.layers[%1].asset").arg(index), QStringLiteral("Referência de imagem inválida."));
                return result;
            }
            const QString imagePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/radiantcursor-studio/assets/sha256/") + assetId.mid(7) + QLatin1Char('.') + extension;
            QImageReader reader(imagePath);
            const QSize imageSize = reader.size();
            if (!imageSize.isValid() || imageSize.width() > 4096 || imageSize.height() > 4096) {
                fail(QStringLiteral("invalid_image"), QStringLiteral("$.layers[%1].asset").arg(index), QStringLiteral("Imagem ausente, inválida ou grande demais."));
                return result;
            }
            QImage decoded = reader.read();
            if (decoded.isNull()) {
                fail(QStringLiteral("image_decode_failed"), QStringLiteral("$.layers[%1].asset").arg(index), reader.errorString());
                return result;
            }
            layer.image = std::make_shared<const QImage>(std::move(decoded));
            layer.imageKey = assetId;
        } else {
            fail(QStringLiteral("unsupported_layer"), QStringLiteral("$.layers[%1].type").arg(index), QStringLiteral("Tipo de layer ainda não suportado pelo runtime nativo."));
            return result;
        }
        const float layerBounds = std::max({layer.geometry.radius * 2.0f, layer.geometry.size.length(), layer.emitter.speed * layer.durationMs / 1000.0f + layer.emitter.spawnRadius});
        compiled->maximumBounds = std::min(4096.0f, std::max(compiled->maximumBounds, layerBounds + 96.0f));
        compiled->layers.push_back(std::move(layer));
    }
    result.effect = std::move(compiled);
    return result;
}
} // namespace KWin::RadiantCursorEngine
