// SPDX-License-Identifier: GPL-3.0-or-later
#include "effectengine.h"
#include "../src/json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <regex>
#include <stdexcept>

namespace rc {
namespace {
using namespace RadiantCursorEngine;
constexpr std::uintmax_t MaxJsonBytes = 512 * 1024;

std::string readFile(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > MaxJsonBytes) throw std::runtime_error("invalid or oversized json file");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read json file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template<typename T> T clampNumber(const json::Value &value, T fallback, T low, T high) {
    return std::clamp(static_cast<T>(value.number(fallback)), low, high);
}

Vec2 vec2(const json::Value &value, Vec2 fallback = {}) {
    if (!value.isArray() || value.size() != 2) return fallback;
    return {static_cast<float>(value.at(0).number(fallback.x)), static_cast<float>(value.at(1).number(fallback.y))};
}

Color color(std::string_view value, Color fallback = {}) {
    if (value.size() != 7 || value[0] != '#') return fallback;
    auto component = [&](std::size_t offset) -> std::optional<int> {
        int result = 0;
        for (std::size_t index = offset; index < offset + 2; ++index) {
            result <<= 4;
            const char digit = value[index];
            if (digit >= '0' && digit <= '9') result += digit - '0';
            else if (digit >= 'a' && digit <= 'f') result += digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') result += digit - 'A' + 10;
            else return std::nullopt;
        }
        return result;
    };
    const auto red = component(1), green = component(3), blue = component(5);
    if (!red || !green || !blue) return fallback;
    return {*red / 255.0f, *green / 255.0f, *blue / 255.0f, 1.0f};
}

std::string text(const json::Value &value, std::string fallback = {}) {
    return value.isString() ? std::string(value.string()) : fallback;
}

Easing easingFromString(std::string_view value) {
    if (value == "easeInQuad") return Easing::EaseInQuad;
    if (value == "easeOutQuad") return Easing::EaseOutQuad;
    if (value == "easeInOutQuad") return Easing::EaseInOutQuad;
    if (value == "easeInCubic") return Easing::EaseInCubic;
    if (value == "easeOutCubic") return Easing::EaseOutCubic;
    if (value == "easeInOutCubic") return Easing::EaseInOutCubic;
    if (value == "easeOutBack") return Easing::EaseOutBack;
    if (value == "easeOutBounce") return Easing::EaseOutBounce;
    if (value == "easeOutElastic") return Easing::EaseOutElastic;
    return Easing::Linear;
}

Settings parseSettings(const json::Value &source) {
    Settings result;
    result.clickEnabled = source.at("ClickEnabled").boolean(true);
    result.colors[0] = color(source.at("Color1").string("#ff0000"));
    result.colors[1] = color(source.at("Color2").string("#00ff00"));
    result.colors[2] = color(source.at("Color3").string("#0000ff"));
    result.lineWidth = clampNumber(source.at("LineWidth"), 2.0f, 0.5f, 99.99f);
    result.lifeMs = clampNumber(source.at("RingLife"), 520, 50, 5000);
    result.size = clampNumber(source.at("RingSize"), 54.0f, 1.0f, 1000.0f);
    result.count = clampNumber(source.at("RingCount"), 3, 1, 99);
    result.showText = source.at("ShowText").boolean(false);
    result.font = text(source.at("Font"), "Segoe UI");
    result.style = text(source.at("Style"), "ripple");
    result.trigger = text(source.at("Trigger"), "press");
    result.glow = source.at("Glow").boolean(true);
    result.trailEnabled = source.at("TrailEnabled").boolean(false);
    result.trailStyle = text(source.at("TrailStyle"), "dots");
    result.trailColor = color(source.at("TrailColor").string("#ffffff"));
    result.trailSize = clampNumber(source.at("TrailSize"), 14.0f, 1.0f, 200.0f);
    result.trailLifeMs = clampNumber(source.at("TrailLife"), 520, 50, 3000);
    result.trailDensity = clampNumber(source.at("TrailDensity"), 65, 1, 100);
    result.trailFrequency = clampNumber(source.at("TrailFrequency"), 30, 1, 240);
    result.trailOpacity = clampNumber(source.at("TrailOpacity"), .72f, .05f, 1.0f);
    result.trailOffsetX = clampNumber(source.at("TrailOffsetX"), 8.0f, -128.0f, 128.0f);
    result.trailOffsetY = clampNumber(source.at("TrailOffsetY"), 8.0f, -128.0f, 128.0f);
    result.cursorTextOffset = {clampNumber(source.at("CursorTextOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorTextOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorLinkOffset = {clampNumber(source.at("CursorLinkOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorLinkOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorCrosshairOffset = {clampNumber(source.at("CursorCrosshairOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorCrosshairOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorBusyOffset = {clampNumber(source.at("CursorBusyOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorBusyOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorMoveOffset = {clampNumber(source.at("CursorMoveOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorMoveOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorForbiddenOffset = {clampNumber(source.at("CursorForbiddenOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorForbiddenOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorHelpOffset = {clampNumber(source.at("CursorHelpOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorHelpOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorResizeHorizontalOffset = {clampNumber(source.at("CursorResizeHorizontalOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorResizeHorizontalOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorResizeVerticalOffset = {clampNumber(source.at("CursorResizeVerticalOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorResizeVerticalOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorResizeDiagonalNwSeOffset = {clampNumber(source.at("CursorResizeDiagonalNwSeOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorResizeDiagonalNwSeOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.cursorResizeDiagonalNeSwOffset = {clampNumber(source.at("CursorResizeDiagonalNeSwOffsetX"), 8.0f, -128.0f, 128.0f), clampNumber(source.at("CursorResizeDiagonalNeSwOffsetY"), 8.0f, -128.0f, 128.0f)};
    result.trailDistance = clampNumber(source.at("TrailDistance"), 0.0f, 0.0f, 128.0f);
    result.trailGlow = source.at("TrailGlow").boolean(true);
    result.trailOnlyPressed = source.at("TrailOnlyPressed").boolean(false);
    result.haloEnabled = source.at("HaloEnabled").boolean(false);
    result.haloStyle = text(source.at("HaloStyle"), "orbitTrail");
    result.haloColor = color(source.at("HaloColor").string("#8bd97b"));
    result.haloSize = clampNumber(source.at("HaloSize"), 18.0f, 1.0f, 200.0f);
    result.haloDistance = clampNumber(source.at("HaloDistance"), 48.0f, 0.0f, 256.0f);
    result.haloDensity = clampNumber(source.at("HaloDensity"), 55, 1, 100);
    result.haloOpacity = clampNumber(source.at("HaloOpacity"), .82f, .05f, 1.0f);
    result.haloSpeed = clampNumber(source.at("HaloSpeed"), 1.0f, 0.0f, 4.0f);
    result.haloVariantIntervalMs = clampNumber(source.at("HaloVariantInterval"), 1400, 10, 5000);
    result.haloCycleVariants = source.at("HaloCycleVariants").boolean(true);
    result.haloGlow = source.at("HaloGlow").boolean(true);
    return result;
}

CompiledEffect parseProgram(const json::Value &root, std::string_view expectedId, std::string_view expectedRevision) {
    CompiledEffect effect;
    if (static_cast<int>(root.at("runtimeVersion").number()) != 1) throw std::runtime_error("unsupported runtime version");
    effect.id = text(root.at("effectId"));
    effect.revision = text(root.at("revision"));
    if (effect.id != expectedId || effect.revision != expectedRevision) throw std::runtime_error("runtime identity mismatch");
    effect.durationMs = clampNumber(root.at("durationMs"), 500, 1, 10000);
    effect.maximumBounds = clampNumber(root.at("maxRadius"), 256.0f, 32.0f, 4096.0f);
    const auto &nodes = root.at("nodes").array();
    if (nodes.empty() || nodes.size() > 128) throw std::runtime_error("invalid runtime node count");
    effect.layers.reserve(nodes.size());

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &source = nodes[index];
        CompiledLayer layer;
        layer.id = text(source.at("id"));
        layer.enabled = source.at("visible").boolean(true);
        layer.groupNode = source.at("kind").string() == "group";
        layer.parentIndex = static_cast<int>(source.at("parentIndex").number(-1));
        layer.startMs = clampNumber(source.at("startMs"), 0, 0, 10000);
        layer.durationMs = clampNumber(source.at("durationMs"), 500, 1, 10000);
        layer.baseOpacity = clampNumber(source.at("opacity"), 1.0f, 0.0f, 1.0f);
        if (layer.id.empty() || layer.parentIndex >= static_cast<int>(index) || layer.parentIndex < -1) throw std::runtime_error("invalid runtime hierarchy");

        const auto &transform = source.at("transform");
        layer.position = vec2(transform.at("position"));
        layer.scale = vec2(transform.at("scale"), {1,1});
        layer.rotationDegrees = static_cast<float>(transform.at("rotationDeg").number());
        layer.geometry.size = vec2(transform.at("size"), {40,40});
        layer.geometry.radius = std::min(layer.geometry.size.x, layer.geometry.size.y) * .5f;

        for (const auto &channelSource : source.at("channels").array()) {
            CompiledLayer::MotionChannel channel;
            const auto property = channelSource.at("property").string();
            if (property == "position.y") channel.property = CompiledLayer::MotionProperty::PositionY;
            else if (property == "rotation") channel.property = CompiledLayer::MotionProperty::Rotation;
            else if (property == "scale.x") channel.property = CompiledLayer::MotionProperty::ScaleX;
            else if (property == "scale.y") channel.property = CompiledLayer::MotionProperty::ScaleY;
            else if (property == "opacity") channel.property = CompiledLayer::MotionProperty::Opacity;
            channel.multiply = channelSource.at("composition").string() == "multiply";
            const auto &frames = channelSource.at("keyframes").array();
            if (frames.size() < 2 || frames.size() > 64) throw std::runtime_error("invalid keyframe count");
            float previous = -1.0f;
            for (const auto &frame : frames) {
                Keyframe key;
                key.time = static_cast<float>(frame.at("time").number(-1));
                key.value = static_cast<float>(frame.at("value").number());
                key.easing = easingFromString(frame.at("easing").string("linear"));
                if (key.time < previous || key.time < 0 || key.time > layer.durationMs) throw std::runtime_error("invalid keyframe time");
                previous = key.time;
                channel.frames.push_back(key);
            }
            layer.motionChannels.push_back(std::move(channel));
        }

        if (!layer.groupNode) {
            const auto &shape = source.at("shape");
            const auto kind = shape.at("kind").string("circle");
            if (kind == "rectangle") layer.geometry.kind = GeometryKind::Rect;
            else if (kind == "line") {
                layer.geometry.kind = GeometryKind::Line;
                layer.geometry.lineStart = {-layer.geometry.size.x * .5f, 0};
                layer.geometry.lineEnd = {layer.geometry.size.x * .5f, 0};
            } else if (kind == "diamond") layer.geometry.kind = GeometryKind::Diamond;
            else if (kind == "star") {
                layer.geometry.kind = GeometryKind::Star;
                layer.geometry.points = clampNumber(shape.at("points"), 5, 2, 32);
                layer.geometry.innerRadius = layer.geometry.radius * clampNumber(shape.at("innerRatio"), .45f, .05f, .95f);
            } else if (kind == "triangle") { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = 3; }
            else if (kind == "hexagon") { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = 6; }
            else if (kind == "polygon") { layer.geometry.kind = GeometryKind::Polygon; layer.geometry.points = clampNumber(shape.at("sides"), 5, 3, 32); }
            else layer.geometry.kind = GeometryKind::Circle;

            const auto &appearance = source.at("appearance");
            const auto &fill = appearance.at("fill");
            const auto &stroke = appearance.at("stroke");
            layer.material.hasFill = fill.at("enabled").boolean(false);
            layer.material.fillColor = color(fill.at("color").string("#ffffff"));
            layer.material.hasStroke = stroke.at("enabled").boolean(true);
            layer.material.strokeColor = color(stroke.at("color").string("#ffffff"));
            layer.material.strokeWidth = clampNumber(stroke.at("width"), 2.0f, .5f, 100.0f);
            layer.material.opacity = clampNumber(appearance.at("opacity"), 1.0f, 0.0f, 1.0f);
            layer.material.blendMode = appearance.at("blendMode").string() == "additive" ? BlendMode::Additive : BlendMode::Normal;
        }
        effect.layers.push_back(std::move(layer));
    }
    return effect;
}

float channelValue(const CompiledLayer::MotionChannel &channel, float time) {
    if (channel.frames.empty()) return channel.multiply ? 1.0f : 0.0f;
    if (time <= channel.frames.front().time) return channel.frames.front().value;
    for (std::size_t index = 1; index < channel.frames.size(); ++index) {
        const auto &a = channel.frames[index - 1], &b = channel.frames[index];
        if (time <= b.time) {
            const float progress = RadiantCursorEngine::evaluateEasing(a.easing, (time - a.time) / std::max(.001f, b.time - a.time));
            return std::lerp(a.value, b.value, progress);
        }
    }
    return channel.frames.back().value;
}

void appendPolygon(RenderCommand &command, const CompiledGeometry &geometry, Vec2 center, Vec2 scale, float rotation) {
    int vertices = geometry.points;
    const bool star = geometry.kind == GeometryKind::Star;
    if (geometry.kind == GeometryKind::Rect || geometry.kind == GeometryKind::Diamond) vertices = 4;
    if (star) vertices *= 2;
    command.points.reserve(static_cast<std::size_t>(vertices));
    const float offset = geometry.kind == GeometryKind::Diamond ? std::numbers::pi_v<float> / 4.0f : -std::numbers::pi_v<float> / 2.0f;
    for (int index = 0; index < vertices; ++index) {
        const float angle = offset + rotation + 2.0f * std::numbers::pi_v<float> * index / vertices;
        float radius = geometry.radius;
        if (geometry.kind == GeometryKind::Rect || geometry.kind == GeometryKind::Diamond) {
            const float x = (index == 0 || index == 3) ? -geometry.size.x * .5f : geometry.size.x * .5f;
            const float y = index < 2 ? -geometry.size.y * .5f : geometry.size.y * .5f;
            const float cosine = std::cos(rotation), sine = std::sin(rotation);
            command.points.push_back({center.x + (x * cosine - y * sine) * scale.x, center.y + (x * sine + y * cosine) * scale.y});
            continue;
        }
        if (star && index % 2 == 1) radius = geometry.innerRadius;
        command.points.push_back({center.x + std::cos(angle) * radius * scale.x, center.y + std::sin(angle) * radius * scale.y});
    }
}

void appendGeometry(const CompiledGeometry &geometry, const CompiledMaterial &material, Vec2 center,
                    Vec2 scale, float rotation, float inheritedOpacity, std::vector<RenderCommand> &commands) {
    const float opacity = std::clamp(material.opacity * inheritedOpacity, 0.0f, 1.0f);
    auto append = [&](bool filled, Color color, float width) {
        color.a *= opacity;
        if (color.a <= 0.0f) return;
        RenderCommand command;
        command.center = center;
        command.width = width;
        command.filled = filled;
        command.color = color;
        command.blendMode = material.blendMode;
        if (geometry.kind == GeometryKind::Circle || geometry.kind == GeometryKind::Ring) {
            command.kind = RenderCommand::Kind::Circle;
            command.radius = geometry.radius * std::max(std::abs(scale.x), std::abs(scale.y));
            command.filled = filled && geometry.kind != GeometryKind::Ring;
        } else if (geometry.kind == GeometryKind::Line) {
            command.kind = RenderCommand::Kind::Lines;
            const float cosine = std::cos(rotation), sine = std::sin(rotation);
            auto transformed = [&](Vec2 point) { return Vec2{center.x + (point.x * cosine - point.y * sine) * scale.x, center.y + (point.x * sine + point.y * cosine) * scale.y}; };
            command.points = {transformed(geometry.lineStart), transformed(geometry.lineEnd)};
        } else {
            command.kind = RenderCommand::Kind::Polygon;
            appendPolygon(command, geometry, center, scale, rotation);
        }
        commands.push_back(std::move(command));
    };
    if (material.hasFill) append(true, material.fillColor, 1.0f);
    if (material.hasStroke) append(false, material.strokeColor, std::max(.5f, material.strokeWidth));
}
} // namespace

namespace RadiantCursorEngine {

float evaluateEasing(Easing easing, float progress) {
    const float x = std::clamp(progress, 0.0f, 1.0f);
    switch (easing) {
    case Easing::EaseInQuad: return x * x;
    case Easing::EaseOutQuad: return 1.0f - (1.0f - x) * (1.0f - x);
    case Easing::EaseInOutQuad: return x < .5f ? 2.0f*x*x : 1.0f-std::pow(-2.0f*x+2.0f,2.0f)/2.0f;
    case Easing::EaseInCubic: return x*x*x;
    case Easing::EaseOutCubic: return 1.0f-std::pow(1.0f-x,3.0f);
    case Easing::EaseInOutCubic: return x < .5f ? 4.0f*x*x*x : 1.0f-std::pow(-2.0f*x+2.0f,3.0f)/2.0f;
    case Easing::EaseOutBack: { constexpr float c1=1.70158f,c3=c1+1.0f; return 1.0f+c3*std::pow(x-1.0f,3.0f)+c1*std::pow(x-1.0f,2.0f); }
    case Easing::EaseOutBounce: { constexpr float n=7.5625f,d=2.75f; if(x<1.0f/d)return n*x*x;if(x<2.0f/d){const float y=x-1.5f/d;return n*y*y+.75f;}if(x<2.5f/d){const float y=x-2.25f/d;return n*y*y+.9375f;}const float y=x-2.625f/d;return n*y*y+.984375f; }
    case Easing::EaseOutElastic: return x==0.0f||x==1.0f?x:std::pow(2.0f,-10.0f*x)*std::sin((x*10.0f-.75f)*2.0f*std::numbers::pi_v<float>/3.0f)+1.0f;
    default: return x;
    }
}

void CompiledEffect::evaluate(int elapsedMs, Vec2 origin, Color buttonColor, int variant,
                              std::vector<RenderCommand> &commands) const {
    (void)buttonColor;
    (void)variant;
    struct WorldState { Vec2 position; Vec2 scale{1,1}; float rotation=0.0f; float opacity=1.0f; int globalStart=0; bool active=true; };
    std::vector<WorldState> states(layers.size());
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto &layer = layers[index];
        WorldState state;
        const WorldState *parent = layer.parentIndex >= 0 && layer.parentIndex < static_cast<int>(index) ? &states[static_cast<std::size_t>(layer.parentIndex)] : nullptr;
        state.globalStart = (parent ? parent->globalStart : 0) + layer.startMs;
        const float localTime = static_cast<float>(elapsedMs - state.globalStart);
        state.active = layer.enabled && (!parent || parent->active) && localTime >= 0.0f && localTime <= layer.durationMs;
        Vec2 localPosition = layer.position, localScale = layer.scale;
        float localRotation = layer.rotationDegrees, localOpacity = layer.baseOpacity;
        for (const auto &channel : layer.motionChannels) {
            const float value = channelValue(channel, localTime);
            switch (channel.property) {
            case CompiledLayer::MotionProperty::PositionX: localPosition.x += value; break;
            case CompiledLayer::MotionProperty::PositionY: localPosition.y += value; break;
            case CompiledLayer::MotionProperty::Rotation: localRotation += value; break;
            case CompiledLayer::MotionProperty::ScaleX: localScale.x *= value; break;
            case CompiledLayer::MotionProperty::ScaleY: localScale.y *= value; break;
            case CompiledLayer::MotionProperty::Opacity: localOpacity *= value; break;
            }
        }
        if (parent) {
            const float angle = parent->rotation * std::numbers::pi_v<float> / 180.0f;
            const Vec2 scaled{localPosition.x * parent->scale.x, localPosition.y * parent->scale.y};
            state.position = {parent->position.x + scaled.x*std::cos(angle)-scaled.y*std::sin(angle), parent->position.y + scaled.x*std::sin(angle)+scaled.y*std::cos(angle)};
            state.scale = {parent->scale.x*localScale.x, parent->scale.y*localScale.y};
            state.rotation = parent->rotation + localRotation;
            state.opacity = parent->opacity * localOpacity;
        } else {
            state.position=localPosition; state.scale=localScale; state.rotation=localRotation; state.opacity=localOpacity;
        }
        states[index] = state;
        if (!state.active || layer.groupNode) continue;
        appendGeometry(layer.geometry, layer.material, {origin.x+state.position.x,origin.y+state.position.y}, state.scale,
                       state.rotation*std::numbers::pi_v<float>/180.0f, state.opacity, commands);
    }
}

} // namespace RadiantCursorEngine

float deterministicRandom(int index, int variant, int channel) {
    unsigned value=unsigned(index+1)*0x9e3779b9U;value^=unsigned(variant+11)*0x85ebca6bU;value^=unsigned(channel+23)*0xc2b2ae35U;value^=value>>16;value*=0x7feb352dU;value^=value>>15;return float(value&0xffffU)/65535.0f;
}

RuntimeConfiguration loadConfiguration(const std::filesystem::path &dataDirectory) {
    RuntimeConfiguration result;
    const auto root = json::parse(readFile(dataDirectory / "runtime" / "state.json"));
    if (static_cast<int>(root.at("schemaVersion").number()) != 1) throw std::runtime_error("unsupported state schema");
    result.enabled = root.at("enabled").boolean(false);
    result.settings = parseSettings(root.at("settings"));
    result.activeEffectId = text(root.at("activeEffectId"));
    result.activeRevision = text(root.at("activeRevision"));
    static const std::regex safeId("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$"), safeRevision("^sha256:[a-f0-9]{64}$");
    if (!result.activeEffectId.empty() || !result.activeRevision.empty()) {
        if (!std::regex_match(result.activeEffectId,safeId) || !std::regex_match(result.activeRevision,safeRevision)) throw std::runtime_error("unsafe active revision");
        const auto runtimePath=dataDirectory/"library"/"effects"/result.activeEffectId/"revisions"/result.activeRevision.substr(7)/"runtime.json";
        result.program=parseProgram(json::parse(readFile(runtimePath)),result.activeEffectId,result.activeRevision);
    }
    result.activeHaloEffectId = text(root.at("activeHaloEffectId"));
    result.activeHaloRevision = text(root.at("activeHaloRevision"));
    if (!result.activeHaloEffectId.empty() || !result.activeHaloRevision.empty()) {
        if (!std::regex_match(result.activeHaloEffectId,safeId) || !std::regex_match(result.activeHaloRevision,safeRevision)) throw std::runtime_error("unsafe active halo revision");
        const auto runtimePath=dataDirectory/"library"/"effects"/result.activeHaloEffectId/"revisions"/result.activeHaloRevision.substr(7)/"runtime.json";
        result.haloProgram=parseProgram(json::parse(readFile(runtimePath)),result.activeHaloEffectId,result.activeHaloRevision);
    }
    return result;
}

} // namespace rc
