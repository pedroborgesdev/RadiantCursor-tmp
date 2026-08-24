// SPDX-License-Identifier: GPL-3.0-or-later

#include "radiantcursoreffect.h"

#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "opengl/glvertexbuffer.h"

#include <KConfigGroup>

#include <QFontMetrics>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <numbers>

#include <epoxy/gl.h>

namespace KWin
{

namespace
{
constexpr int ButtonCount = 3;
constexpr int CircleSegments = 72;

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

QColor withAlpha(const QColor &color, float alpha)
{
    QColor result(color);
    result.setAlphaF(clamp01(alpha));
    return result;
}

float layoutValue(int index, int variant, int channel = 0)
{
    unsigned int value = unsigned(index + 1) * 0x9e3779b9U;
    value ^= unsigned(variant + 11) * 0x85ebca6bU;
    value ^= unsigned(channel + 23) * 0xc2b2ae35U;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return float(value & 0xffffU) / 65535.0f;
}

float layoutRotation(int variant)
{
    static constexpr float rotations[] = {0.0f, 0.73f, 1.91f, 3.28f};
    return rotations[std::clamp(variant, 0, 3)];
}
}

RadiantCursorEffect::RadiantCursorEffect()
{
    m_renderCommands.reserve(4096);
    m_trailEmissionTimer.start();
    reconfigure(ReconfigureAll);
    connect(effects, &EffectsHandler::mouseChanged, this, &RadiantCursorEffect::handleMouseChanged);
}

RadiantCursorEffect::~RadiantCursorEffect() = default;

void RadiantCursorEffect::reconfigure(ReconfigureFlags)
{
    const KConfigGroup config(effects->config(), QStringLiteral("Effect-radiantcursor"));
    m_colors[0] = config.readEntry("Color1", QColor(QStringLiteral("#67e8f9")));
    m_colors[1] = config.readEntry("Color2", QColor(QStringLiteral("#a78bfa")));
    m_colors[2] = config.readEntry("Color3", QColor(QStringLiteral("#fb7185")));
    m_lineWidth = std::clamp(config.readEntry("LineWidth", 3.0), 0.0, 99.99);
    m_life = std::clamp(config.readEntry("RingLife", 560), 50, 5000);
    m_size = std::clamp(config.readEntry("RingSize", 54), 1, 1000);
    m_count = std::clamp(config.readEntry("RingCount", 3), 1, 99);
    m_showText = config.readEntry("ShowText", false);
    m_glow = config.readEntry("Glow", true);
    m_clickEnabled = config.readEntry("ClickEnabled", true);
    m_trailEnabled = config.readEntry("TrailEnabled", false);
    m_trailGlow = config.readEntry("TrailGlow", true);
    m_trailOnlyPressed = config.readEntry("TrailOnlyPressed", false);
    m_trailColor = config.readEntry("TrailColor", QColor(QStringLiteral("#ffffff")));
    m_trailSize = std::clamp(config.readEntry("TrailSize", 14.0), 1.0, 200.0);
    m_trailLife = std::clamp(config.readEntry("TrailLife", 520), 50, 3000);
    m_trailDensity = std::clamp(config.readEntry("TrailDensity", 65), 1, 100);
    m_trailFrequency = std::clamp(config.readEntry("TrailFrequency", 30), 1, 240);
    m_trailOpacity = std::clamp(config.readEntry("TrailOpacity", 0.72), 0.05, 1.0);
    m_font = config.readEntry("Font", QFont(QStringLiteral("Noto Sans"), 10));

    const QString style = config.readEntry("Style", QStringLiteral("ripple")).toLower();
    static const QStringList validStyles = {
        QStringLiteral("ripple"), QStringLiteral("pulse"), QStringLiteral("target"),
        QStringLiteral("burst"), QStringLiteral("spark"), QStringLiteral("focus"),
        QStringLiteral("halo"), QStringLiteral("shockwave"), QStringLiteral("orbit"),
        QStringLiteral("petals"), QStringLiteral("diamond"), QStringLiteral("sonar"),
        QStringLiteral("vortex"), QStringLiteral("cross"), QStringLiteral("confetti"),
        QStringLiteral("lightning"), QStringLiteral("bubbles"), QStringLiteral("heart"),
        QStringLiteral("ink"), QStringLiteral("splash"), QStringLiteral("nova"),
        QStringLiteral("comet"), QStringLiteral("eclipse"), QStringLiteral("plasma"),
        QStringLiteral("pixelburst"), QStringLiteral("prism"), QStringLiteral("flower"),
        QStringLiteral("meteor"),
    };
    m_style = validStyles.contains(style) ? style : QStringLiteral("ripple");

    const QString trailStyle = config.readEntry("TrailStyle", QStringLiteral("dots"));
    static const QStringList validTrailStyles = {
        QStringLiteral("dots"), QStringLiteral("soft"), QStringLiteral("neon"),
        QStringLiteral("cometTrail"), QStringLiteral("smoke"), QStringLiteral("sparks"),
        QStringLiteral("bubbleTrail"), QStringLiteral("stars"), QStringLiteral("hearts"),
        QStringLiteral("squares"), QStringLiteral("diamonds"), QStringLiteral("triangles"),
        QStringLiteral("ribbon"), QStringLiteral("laser"), QStringLiteral("fire"),
        QStringLiteral("ice"), QStringLiteral("petalTrail"), QStringLiteral("pixels"),
        QStringLiteral("orbitTrail"), QStringLiteral("rainbow"),
    };
    m_trailStyle = validTrailStyles.contains(trailStyle) ? trailStyle : QStringLiteral("dots");

    const QString trigger = config.readEntry("Trigger", QStringLiteral("press")).toLower();
    if (trigger == QLatin1String("release")) {
        m_trigger = Trigger::Release;
    } else if (trigger == QLatin1String("both")) {
        m_trigger = Trigger::Both;
    } else {
        m_trigger = Trigger::Press;
    }

    const QString activeEffectId = config.readEntry("ActiveEffectId", QString());
    const QString activeRevision = config.readEntry("ActiveRevision", QString());
    if (!activeEffectId.isEmpty() && !activeRevision.isEmpty()) {
        RadiantCursorEngine::LoadResult loaded = RadiantCursorEngine::EffectLoader::load(activeEffectId, activeRevision);
        m_programDiagnostics = loaded.diagnostics;
        if (loaded.effect) {
            m_program = std::move(loaded.effect);
        }
    } else {
        m_program.reset();
        m_programDiagnostics.clear();
    }

    m_trailPoints.clear();
    m_lastTrailEmission = -1000;
    m_previousDamage = Region();
    effects->addRepaintFull();
}

void RadiantCursorEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    const int delta = m_lastPresentTime.count()
        ? std::max(0, int((presentTime - m_lastPresentTime).count()))
        : 0;
    for (ClickEvent &event : m_events) {
        event.elapsed += delta;
    }
    for (TrailPoint &point : m_trailPoints) {
        point.elapsed += delta;
    }
    const bool removedEvents = std::erase_if(
        m_events,
        [](const ClickEvent &event) { return event.elapsed > event.lifetime; }) > 0;
    bool removedTrailPoints = false;
    while (!m_trailPoints.empty() && m_trailPoints.front().elapsed > m_trailLife) {
        m_trailPoints.pop_front();
        removedTrailPoints = true;
    }
    m_lastPresentTime = (m_events.empty() && m_trailPoints.empty())
        ? std::chrono::milliseconds::zero() : presentTime;
    effects->prePaintScreen(data, presentTime);
    if (removedEvents || removedTrailPoints) {
        // KWin may stop invoking the effect as soon as isActive() becomes false.
        // Preserve the previous damage for one final frame so the scene below the
        // expired animation is repainted instead of retaining its last pixels.
        repaintEvents();
    }
}

void RadiantCursorEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion, LogicalOutput *screen)
{
    effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
    if (m_events.empty() && m_trailPoints.empty()) {
        return;
    }

    if (effects->isOpenGLCompositing()) {
        GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::UniformColor | ShaderTrait::TransformColorspace);
        shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
        shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_drawingTrail = true;
    for (const TrailPoint &point : m_trailPoints) {
        drawTrailPoint(point, viewport);
    }
    m_drawingTrail = false;
    for (const ClickEvent &event : m_events) {
        drawEvent(event, renderTarget, viewport);
    }

    if (effects->isOpenGLCompositing()) {
        ShaderManager::instance()->popShader();
        if (m_showText) {
            for (ClickEvent &event : m_events) {
                drawLabel(event, renderTarget, viewport, eventAlpha(float(event.elapsed) / float(event.lifetime)));
            }
        }
        glDisable(GL_BLEND);
    } else if (m_showText && effects->compositingType() == QPainterCompositing) {
        QPainter *painter = effects->scenePainter();
        painter->save();
        painter->setFont(m_font);
        for (const ClickEvent &event : m_events) {
            QColor textColor(Qt::white);
            textColor.setAlphaF(eventAlpha(float(event.elapsed) / float(event.lifetime)));
            painter->setPen(textColor);
            const QString suffix = event.pressed ? QStringLiteral(" ↓") : QStringLiteral(" ↑");
            const QString labels[] = {QStringLiteral("Esquerdo"), QStringLiteral("Meio"), QStringLiteral("Direito")};
            painter->drawText(event.position + QPointF(m_size + 8, 4), labels[event.button] + suffix);
        }
        painter->restore();
    }
}

void RadiantCursorEffect::postPaintScreen()
{
    effects->postPaintScreen();
    repaintEvents();
}

bool RadiantCursorEffect::isActive() const
{
    return !m_events.empty() || !m_trailPoints.empty();
}

void RadiantCursorEffect::handleMouseChanged(const QPointF &position, const QPointF &oldPosition,
                                       Qt::MouseButtons buttons, Qt::MouseButtons oldButtons,
                                       Qt::KeyboardModifiers, Qt::KeyboardModifiers)
{
    const QVector2D movement(position - oldPosition);
    const float distance = movement.length();
    const qint64 now = m_trailEmissionTimer.elapsed();
    const qint64 minimumInterval = std::max<qint64>(
        1, qint64(std::ceil(1000.0 / double(m_trailFrequency))));
    const bool frequencyAllowsEmission = now - m_lastTrailEmission >= minimumInterval;
    if (m_trailEnabled && frequencyAllowsEmission &&
        (!m_trailOnlyPressed || buttons != Qt::NoButton) && distance > 0.01f) {
        m_lastTrailEmission = now;
        const float spacing = 2.5f + (100 - m_trailDensity) * 0.42f;
        const int samples = std::clamp(int(std::floor(distance / spacing)), 1, 6);
        const QVector2D movementDirection = movement.normalized();
        const float movementAngle = std::atan2(movementDirection.y(), movementDirection.x());
        const int particlesPerSample = 2 + ((m_trailDensity - 1) * 4 / 99);
        static constexpr int variantOrder[] = {0, 2, 3, 1};
        const unsigned int emissionSerial = m_trailSequence++;
        const int variant = variantOrder[emissionSerial % 4];
        for (int sample = 1; sample <= samples; ++sample) {
            const float amount = float(sample) / float(samples);
            const QPointF samplePosition = oldPosition + (position - oldPosition) * amount;
            for (int particle = 0; particle < particlesPerSample; ++particle) {
                const int particleIndex = sample * 8 + particle;
                const float arrangementAngle = layoutRotation(variant)
                    + 2.0f * std::numbers::pi_v<float> * particle / particlesPerSample
                    + (layoutValue(particleIndex, variant, 0) - 0.5f) * 0.72f;
                const float spread = m_trailSize
                    * (0.18f + 0.82f * layoutValue(particleIndex, variant, 1));
                const QPointF particlePosition = samplePosition
                    + QPointF(std::cos(arrangementAngle), std::sin(arrangementAngle)) * spread;
                const float directionAngle = movementAngle
                    + (layoutValue(particleIndex, variant, 2) - 0.5f) * 0.9f;
                const QVector2D particleDirection(std::cos(directionAngle), std::sin(directionAngle));
                const float particleScale = 0.48f
                    + 0.62f * layoutValue(particleIndex, variant, 3);
                const unsigned int particleSerial = emissionSerial * 64U + unsigned(particleIndex);
                m_trailPoints.push_back(TrailPoint{
                    particlePosition, particleDirection, particleScale,
                    0, variant, particleSerial,
                });
            }
        }
        while (m_trailPoints.size() > 420) {
            m_trailPoints.pop_front();
        }
    }

    static const Qt::MouseButton mouseButtons[] = {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton};
    for (int index = 0; m_clickEnabled && index < ButtonCount; ++index) {
        const bool pressed = buttonPressed(mouseButtons[index], buttons, oldButtons);
        const bool released = buttonReleased(mouseButtons[index], buttons, oldButtons);
        const bool acceptsPress = m_trigger == Trigger::Press || m_trigger == Trigger::Both;
        const bool acceptsRelease = m_trigger == Trigger::Release || m_trigger == Trigger::Both;
        if ((pressed && acceptsPress) || (released && acceptsRelease)) {
            static constexpr int variantOrder[] = {0, 2, 3, 1};
            const int variant = variantOrder[m_eventSequence++ % 4];
            const int lifetime = m_program ? m_program->durationMs : m_life;
            m_events.push_back(ClickEvent{index, position, 0, pressed, variant, lifetime, m_program, nullptr, {}});
            // Bound compositor work during click storms. Definitions remain shared;
            // only the oldest lightweight instance is discarded.
            while (m_events.size() > 24) {
                m_events.pop_front();
            }
            break;
        }
    }
    repaintEvents();
}

void RadiantCursorEffect::drawTrailPoint(const TrailPoint &point, const RenderViewport &viewport)
{
    const float progress = clamp01(float(point.elapsed) / float(m_trailLife));
    const float fade = (1.0f - progress) * (1.0f - progress);
    const float alpha = m_trailOpacity * fade;
    const float size = m_trailSize * point.scale * (0.62f + 0.38f * fade);
    const QVector2D center(point.position);
    const QVector2D direction = point.direction.lengthSquared() > 0.01f
        ? point.direction.normalized() : QVector2D(1.0f, 0.0f);
    const QVector2D tangent(-direction.y(), direction.x());
    QColor color = m_trailColor;
    if (m_trailStyle == QLatin1String("rainbow")) {
        color = QColor::fromHsvF(std::fmod(point.serial * 0.047f + point.variant * 0.17f, 1.0f), 0.78f, 1.0f);
    }

    auto regularPolygon = [&](int sides, float radius, float rotation = 0.0f) {
        QList<QVector2D> vertices;
        vertices.reserve(sides);
        for (int index = 0; index < sides; ++index) {
            const float angle = rotation + 2.0f * std::numbers::pi_v<float> * index / sides;
            vertices << center + QVector2D(std::cos(angle), std::sin(angle)) * radius;
        }
        return vertices;
    };

    if (m_trailStyle == QLatin1String("soft")) {
        drawCircle(viewport, withAlpha(color, alpha * 0.12f), point.position, size * 1.5f, 1.0f, true);
        drawCircle(viewport, withAlpha(color, alpha * 0.38f), point.position, size * 0.72f, 1.0f, true);
    } else if (m_trailStyle == QLatin1String("neon")) {
        const QList<QVector2D> segment = {center - direction * size * 2.2f, center + direction * size * 0.2f};
        drawLines(viewport, withAlpha(color, alpha * 0.2f), segment, std::max(4.0f, size * 0.75f), GL_LINES);
        drawLines(viewport, withAlpha(color.lighter(150), alpha * 0.9f), segment, std::max(1.0f, size * 0.2f), GL_LINES);
    } else if (m_trailStyle == QLatin1String("cometTrail")) {
        QList<QVector2D> tail = {
            center + tangent * size * 0.48f,
            center - direction * size * 3.2f,
            center - tangent * size * 0.48f,
        };
        drawPolygon(viewport, withAlpha(color, alpha * 0.28f), tail);
        drawCircle(viewport, withAlpha(color.lighter(140), alpha * 0.92f), point.position, size * 0.58f, 1.0f, true);
    } else if (m_trailStyle == QLatin1String("smoke")) {
        const QPointF drifted = point.position + QPointF(0, -progress * size * 2.4f)
            + tangent.toPointF() * ((point.variant - 1.5f) * size * 0.24f);
        drawCircle(viewport, withAlpha(color, alpha * 0.1f), drifted, size * (1.1f + progress), 1.0f, true);
        drawCircle(viewport, withAlpha(color, alpha * 0.18f), drifted, size * (0.58f + progress * 0.42f), 1.0f, true);
    } else if (m_trailStyle == QLatin1String("sparks")) {
        QList<QVector2D> rays;
        for (int ray = 0; ray < 4; ++ray) {
            const float angle = layoutRotation(point.variant) + ray * std::numbers::pi_v<float> / 2.0f;
            const QVector2D rayDirection(std::cos(angle), std::sin(angle));
            rays << center + rayDirection * size * 0.25f << center + rayDirection * size * (0.7f + 0.35f * fade);
        }
        drawLines(viewport, withAlpha(color, alpha * 0.85f), rays, std::max(1.0f, size * 0.16f), GL_LINES);
    } else if (m_trailStyle == QLatin1String("bubbleTrail")) {
        drawCircle(viewport, withAlpha(color, alpha * 0.72f), point.position, size * 0.72f, std::max(1.0f, size * 0.13f));
        drawCircle(viewport, withAlpha(color.lighter(160), alpha * 0.7f), point.position + QPointF(-size * 0.2f, -size * 0.2f), size * 0.12f, 1.0f, true);
    } else if (m_trailStyle == QLatin1String("stars")) {
        QList<QVector2D> star;
        for (int index = 0; index < 10; ++index) {
            const float angle = layoutRotation(point.variant) - std::numbers::pi_v<float> / 2.0f + index * std::numbers::pi_v<float> / 5.0f;
            const float radius = index % 2 == 0 ? size : size * 0.42f;
            star << center + QVector2D(std::cos(angle), std::sin(angle)) * radius;
        }
        drawPolygon(viewport, withAlpha(color, alpha * 0.76f), star);
    } else if (m_trailStyle == QLatin1String("hearts")) {
        QList<QVector2D> heart;
        const float scale = size / 18.0f;
        for (int index = 0; index < 30; ++index) {
            const float t = 2.0f * std::numbers::pi_v<float> * index / 30.0f;
            const float x = 16.0f * std::pow(std::sin(t), 3.0f);
            const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
            heart << center + QVector2D(x * scale, -y * scale);
        }
        drawPolygon(viewport, withAlpha(color, alpha * 0.68f), heart);
    } else if (m_trailStyle == QLatin1String("squares")) {
        drawPolygon(viewport, withAlpha(color, alpha * 0.72f), regularPolygon(4, size * 0.78f, layoutRotation(point.variant)));
    } else if (m_trailStyle == QLatin1String("diamonds")) {
        drawPolygon(viewport, withAlpha(color, alpha * 0.78f), regularPolygon(4, size, std::numbers::pi_v<float> / 4.0f));
    } else if (m_trailStyle == QLatin1String("triangles")) {
        const float angle = std::atan2(direction.y(), direction.x());
        drawPolygon(viewport, withAlpha(color, alpha * 0.78f), regularPolygon(3, size, angle));
    } else if (m_trailStyle == QLatin1String("ribbon")) {
        const QList<QVector2D> ribbon = {
            center - direction * size * 2.0f + tangent * size * 0.3f,
            center + direction * size * 0.35f - tangent * size * 0.3f,
        };
        drawLines(viewport, withAlpha(color, alpha * 0.68f), ribbon, std::max(2.0f, size * 0.52f), GL_LINES);
    } else if (m_trailStyle == QLatin1String("laser")) {
        const QList<QVector2D> beam = {center - direction * size * 2.8f, center + direction * size * 0.25f};
        drawLines(viewport, withAlpha(color, alpha * 0.22f), beam, std::max(4.0f, size * 0.62f), GL_LINES);
        drawLines(viewport, withAlpha(Qt::white, alpha * 0.9f), beam, std::max(1.0f, size * 0.13f), GL_LINES);
    } else if (m_trailStyle == QLatin1String("fire")) {
        const QVector2D tip = center - direction * size * (1.6f + progress);
        QList<QVector2D> flame = {center + tangent * size * 0.58f, tip, center - tangent * size * 0.58f};
        drawPolygon(viewport, withAlpha(color, alpha * 0.48f), flame);
        drawCircle(viewport, withAlpha(color.lighter(165), alpha * 0.84f), point.position, size * 0.42f, 1.0f, true);
    } else if (m_trailStyle == QLatin1String("ice")) {
        QList<QVector2D> crystal;
        for (int arm = 0; arm < 6; ++arm) {
            const float angle = arm * std::numbers::pi_v<float> / 3.0f + layoutRotation(point.variant);
            const QVector2D armDirection(std::cos(angle), std::sin(angle));
            crystal << center - armDirection * size * 0.78f << center + armDirection * size * 0.78f;
        }
        drawLines(viewport, withAlpha(color.lighter(145), alpha * 0.82f), crystal, std::max(1.0f, size * 0.12f), GL_LINES);
    } else if (m_trailStyle == QLatin1String("petalTrail")) {
        for (int petal = 0; petal < 3; ++petal) {
            const float angle = layoutRotation(point.variant) + petal * 2.0f * std::numbers::pi_v<float> / 3.0f;
            const QPointF petalCenter = point.position + QPointF(std::cos(angle), std::sin(angle)) * size * 0.38f;
            drawCircle(viewport, withAlpha(color, alpha * 0.48f), petalCenter, size * 0.52f, 1.0f, true);
        }
    } else if (m_trailStyle == QLatin1String("pixels")) {
        for (int pixel = 0; pixel < 3; ++pixel) {
            const float offset = (pixel - 1) * size * 0.72f;
            const QVector2D pixelCenter = center - direction * pixel * size * 0.46f + tangent * offset;
            QList<QVector2D> square;
            const float half = size * (0.34f - pixel * 0.05f);
            square << pixelCenter + QVector2D(-half, -half) << pixelCenter + QVector2D(half, -half)
                   << pixelCenter + QVector2D(half, half) << pixelCenter + QVector2D(-half, half);
            drawPolygon(viewport, withAlpha(color, alpha * (0.72f - pixel * 0.16f)), square);
        }
    } else if (m_trailStyle == QLatin1String("orbitTrail")) {
        drawCircle(viewport, withAlpha(color, alpha * 0.3f), point.position, size * 0.82f, std::max(1.0f, size * 0.08f));
        for (int satellite = 0; satellite < 2; ++satellite) {
            const float angle = progress * 8.0f + layoutRotation(point.variant) + satellite * std::numbers::pi_v<float>;
            const QPointF satellitePoint = point.position + QPointF(std::cos(angle), std::sin(angle)) * size * 0.82f;
            drawCircle(viewport, withAlpha(color, alpha * 0.82f), satellitePoint, size * 0.2f, 1.0f, true);
        }
    } else if (m_trailStyle == QLatin1String("rainbow")) {
        drawCircle(viewport, withAlpha(color, alpha * 0.72f), point.position, size * 0.62f, 1.0f, true);
        drawCircle(viewport, withAlpha(color.lighter(145), alpha * 0.25f), point.position, size, 1.0f, true);
    } else {
        drawCircle(viewport, withAlpha(color, alpha * 0.82f), point.position, size * 0.54f, 1.0f, true);
    }
}

void RadiantCursorEffect::drawEvent(const ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport)
{
    if (event.program) {
        drawDeclarativeEvent(event, renderTarget, viewport);
        return;
    }
    const float progress = clamp01(float(event.elapsed) / float(m_life));
    const QColor color = m_colors[event.button];
    if (m_style == QLatin1String("pulse")) {
        drawPulse(event, viewport, color, progress);
    } else if (m_style == QLatin1String("target")) {
        drawTarget(event, viewport, color, progress);
    } else if (m_style == QLatin1String("burst")) {
        drawBurst(event, viewport, color, progress);
    } else if (m_style == QLatin1String("spark")) {
        drawSpark(event, viewport, color, progress);
    } else if (m_style == QLatin1String("focus")) {
        drawFocus(event, viewport, color, progress);
    } else if (m_style == QLatin1String("halo")) {
        drawHalo(event, viewport, color, progress);
    } else if (m_style == QLatin1String("shockwave")) {
        drawShockwave(event, viewport, color, progress);
    } else if (m_style == QLatin1String("orbit")) {
        drawOrbit(event, viewport, color, progress);
    } else if (m_style == QLatin1String("petals")) {
        drawPetals(event, viewport, color, progress);
    } else if (m_style == QLatin1String("diamond")) {
        drawDiamond(event, viewport, color, progress);
    } else if (m_style == QLatin1String("sonar")) {
        drawSonar(event, viewport, color, progress);
    } else if (m_style == QLatin1String("vortex")) {
        drawVortex(event, viewport, color, progress);
    } else if (m_style == QLatin1String("cross")) {
        drawCross(event, viewport, color, progress);
    } else if (m_style == QLatin1String("confetti")) {
        drawConfetti(event, viewport, color, progress);
    } else if (m_style == QLatin1String("lightning")) {
        drawLightning(event, viewport, color, progress);
    } else if (m_style == QLatin1String("bubbles")) {
        drawBubbles(event, viewport, color, progress);
    } else if (m_style == QLatin1String("heart")) {
        drawHeart(event, viewport, color, progress);
    } else if (m_style == QLatin1String("ink")) {
        drawInk(event, viewport, color, progress);
    } else if (m_style == QLatin1String("splash")) {
        drawSplash(event, viewport, color, progress);
    } else if (m_style == QLatin1String("nova")) {
        drawNova(event, viewport, color, progress);
    } else if (m_style == QLatin1String("comet")) {
        drawComet(event, viewport, color, progress);
    } else if (m_style == QLatin1String("eclipse")) {
        drawEclipse(event, viewport, color, progress);
    } else if (m_style == QLatin1String("plasma")) {
        drawPlasma(event, viewport, color, progress);
    } else if (m_style == QLatin1String("pixelburst")) {
        drawPixelBurst(event, viewport, color, progress);
    } else if (m_style == QLatin1String("prism")) {
        drawPrism(event, viewport, color, progress);
    } else if (m_style == QLatin1String("flower")) {
        drawFlower(event, viewport, color, progress);
    } else if (m_style == QLatin1String("meteor")) {
        drawMeteor(event, viewport, color, progress);
    } else {
        drawRipple(event, viewport, color, progress);
    }
}

void RadiantCursorEffect::drawDeclarativeEvent(const ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport)
{
    m_renderCommands.clear();
    event.program->evaluate(event.elapsed, event.position, m_colors[event.button], event.variant, m_renderCommands);
    RadiantCursorEngine::BlendMode activeBlend = RadiantCursorEngine::BlendMode::Normal;
    for (const RadiantCursorEngine::RenderCommand &command : std::as_const(m_renderCommands)) {
        if (effects->isOpenGLCompositing() && command.blendMode != activeBlend) {
            activeBlend = command.blendMode;
            glBlendFunc(GL_SRC_ALPHA, activeBlend == RadiantCursorEngine::BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        }
        if (command.kind == RadiantCursorEngine::RenderCommand::Kind::Image && command.image) {
            if (effects->isOpenGLCompositing()) {
                std::shared_ptr<GLTexture> &texture = m_imageTextures[command.imageKey];
                if (!texture) {
                    std::unique_ptr<GLTexture> uploaded = GLTexture::upload(*command.image);
                    if (uploaded) {
                        uploaded->setFilter(GL_LINEAR);
                        uploaded->setWrapMode(GL_CLAMP_TO_EDGE);
                        texture = std::shared_ptr<GLTexture>(std::move(uploaded));
                    }
                }
                if (texture) {
                    const QSizeF pixelSize = command.imageSize * viewport.scale();
                    const QPointF pixelCenter = command.center * viewport.scale();
                    GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::TransformColorspace);
                    QMatrix4x4 matrix = viewport.projectionMatrix();
                    matrix.translate(pixelCenter.x(), pixelCenter.y());
                    matrix.rotate(command.rotationRadians * 180.0f / std::numbers::pi_v<float>, 0.0f, 0.0f, 1.0f);
                    matrix.translate(-pixelSize.width() * 0.5f, -pixelSize.height() * 0.5f);
                    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
                    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1.0f, 1.0f, 1.0f, command.color.alphaF()));
                    shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
                    texture->render(pixelSize);
                    ShaderManager::instance()->popShader();
                }
            } else if (effects->compositingType() == QPainterCompositing) {
                QPainter *painter = effects->scenePainter();
                painter->save();
                painter->setOpacity(command.color.alphaF());
                painter->translate(command.center);
                painter->rotate(command.rotationRadians * 180.0f / std::numbers::pi_v<float>);
                painter->drawImage(QRectF(-command.imageSize.width() * 0.5f, -command.imageSize.height() * 0.5f,
                                          command.imageSize.width(), command.imageSize.height()), *command.image);
                painter->restore();
            }
            continue;
        }
        if (command.kind == RadiantCursorEngine::RenderCommand::Kind::Circle) {
            drawCircle(viewport, command.color, command.center, command.radius, command.width, command.filled);
            continue;
        }
        QList<QVector2D> points;
        points.reserve(command.points.size());
        for (const QVector2D &point : command.points) {
            points.push_back(point);
        }
        if (command.kind == RadiantCursorEngine::RenderCommand::Kind::Polygon && command.filled) {
            drawPolygon(viewport, command.color, points);
        } else {
            drawLines(viewport, command.color, points, command.width,
                      command.kind == RadiantCursorEngine::RenderCommand::Kind::Polygon ? GL_LINE_LOOP : GL_LINES);
        }
    }
    if (effects->isOpenGLCompositing() && activeBlend != RadiantCursorEngine::BlendMode::Normal) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

void RadiantCursorEffect::drawRipple(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int rings = std::min(m_count, 24);
    for (int index = 0; index < rings; ++index) {
        const float delay = float(index) / float(std::max(4, rings * 4));
        const float local = clamp01((progress - delay) / (1.0f - delay));
        if (progress < delay) {
            continue;
        }
        const float alpha = eventAlpha(local) * (1.0f - float(index) / float(rings + 2) * 0.35f);
        drawCircle(viewport, withAlpha(color, alpha), event.position, m_size * easeOut(local), m_lineWidth);
    }
}

void RadiantCursorEffect::drawPulse(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float radius = m_size * (0.18f + eased * 0.82f);
    const float alpha = eventAlpha(progress);
    drawCircle(viewport, withAlpha(color, alpha * 0.17f), event.position, radius, m_lineWidth, true);
    drawCircle(viewport, withAlpha(color, alpha), event.position, radius, m_lineWidth);
    drawCircle(viewport, withAlpha(color, alpha * 0.75f), event.position, radius * (0.28f + 0.35f * progress), std::max(1.0f, m_lineWidth * 0.65f));
}

void RadiantCursorEffect::drawTarget(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float radius = m_size * (0.42f + 0.58f * eased);
    const float alpha = eventAlpha(progress);
    drawCircle(viewport, withAlpha(color, alpha), event.position, radius, m_lineWidth);
    drawCircle(viewport, withAlpha(color, alpha * 0.7f), event.position, radius * 0.38f, std::max(1.0f, m_lineWidth * 0.7f));
    QList<QVector2D> lines;
    for (int axis = 0; axis < 4; ++axis) {
        const float angle = axis * std::numbers::pi_v<float> / 2.0f;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D center(event.position);
        lines << center + direction * (radius * 1.12f) << center + direction * (radius * 1.55f);
    }
    drawLines(viewport, withAlpha(color, alpha * 0.9f), lines, m_lineWidth, GL_LINES);
}

void RadiantCursorEffect::drawBurst(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int rays = std::clamp(m_count * 2, 8, 32);
    const float eased = easeOut(progress);
    const float inner = m_size * (0.08f + eased * 0.52f);
    const float outer = m_size * (0.42f + eased * 0.82f);
    const float rotation = progress * 0.28f;
    QList<QVector2D> lines;
    for (int index = 0; index < rays; ++index) {
        const float angle = (2.0f * std::numbers::pi_v<float> * index / rays) + rotation;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D center(event.position);
        lines << center + direction * inner << center + direction * outer;
    }
    drawLines(viewport, withAlpha(color, eventAlpha(progress)), lines, m_lineWidth, GL_LINES);
}

void RadiantCursorEffect::drawSpark(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int points = std::clamp(m_count + 3, 5, 16);
    const float radius = m_size * (0.2f + 0.8f * easeOut(progress));
    const float rotation = -0.2f + progress * 0.7f;
    QList<QVector2D> star;
    const QVector2D center(event.position);
    for (int index = 0; index < points * 2; ++index) {
        const float angle = rotation - std::numbers::pi_v<float> / 2.0f + std::numbers::pi_v<float> * index / points;
        const float pointRadius = index % 2 == 0 ? radius : radius * 0.34f;
        star << center + QVector2D(std::cos(angle), std::sin(angle)) * pointRadius;
    }
    const float alpha = eventAlpha(progress);
    drawLines(viewport, withAlpha(color, alpha), star, m_lineWidth, GL_LINE_LOOP);

    QList<QVector2D> sparks;
    for (int index = 0; index < points; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / points + rotation;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        sparks << center + direction * (radius * 1.12f) << center + direction * (radius * 1.34f);
    }
    drawLines(viewport, withAlpha(color, alpha * 0.68f), sparks, std::max(1.0f, m_lineWidth * 0.7f), GL_LINES);
}

void RadiantCursorEffect::drawFocus(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float half = m_size * (0.38f + 0.62f * easeOut(progress));
    const float arm = half * 0.48f;
    const QVector2D center(event.position);
    QList<QVector2D> corners;
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            const QVector2D corner = center + QVector2D(sx * half, sy * half);
            corners << corner << corner + QVector2D(-sx * arm, 0);
            corners << corner << corner + QVector2D(0, -sy * arm);
        }
    }
    drawLines(viewport, withAlpha(color, eventAlpha(progress)), corners, m_lineWidth, GL_LINES);
}

void RadiantCursorEffect::drawHalo(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float alpha = eventAlpha(progress);
    const float breathe = 1.0f + 0.08f * std::sin(progress * std::numbers::pi_v<float> * 3.0f);
    drawCircle(viewport, withAlpha(color, alpha * 0.16f), event.position, m_size * eased * breathe, std::max(6.0f, m_lineWidth * 4.5f));
    drawCircle(viewport, withAlpha(color, alpha * 0.75f), event.position, m_size * eased * breathe, m_lineWidth);
    drawCircle(viewport, withAlpha(color, alpha * 0.42f), event.position, m_size * (0.52f + eased * 0.2f), std::max(1.0f, m_lineWidth * 0.65f));
}

void RadiantCursorEffect::drawShockwave(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float alpha = eventAlpha(progress);
    const float radius = m_size * eased;
    drawCircle(viewport, withAlpha(color, alpha * 0.22f), event.position, radius, std::max(5.0f, m_lineWidth * 3.5f));
    drawCircle(viewport, withAlpha(color, alpha), event.position, radius, m_lineWidth);
    if (progress > 0.12f) {
        const float echo = clamp01((progress - 0.12f) / 0.88f);
        drawCircle(viewport, withAlpha(color, eventAlpha(echo) * 0.62f), event.position, m_size * easeOut(echo) * 0.72f, std::max(1.0f, m_lineWidth * 0.7f));
    }
}

void RadiantCursorEffect::drawOrbit(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float alpha = eventAlpha(progress);
    const float radius = m_size * (0.3f + 0.7f * easeOut(progress));
    drawCircle(viewport, withAlpha(color, alpha * 0.48f), event.position, radius, std::max(1.0f, m_lineWidth * 0.55f));
    const int satellites = std::clamp(m_count, 2, 8);
    for (int index = 0; index < satellites; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / satellites
            + progress * std::numbers::pi_v<float> * 2.4f + layoutRotation(event.variant)
            + (layoutValue(index, event.variant, 0) - 0.5f) * 0.22f;
        const QPointF point = event.position + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
        drawCircle(viewport, withAlpha(color, alpha), point, std::max(2.5f, m_lineWidth * 1.25f), 1.0f, true);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.85f), event.position, std::max(2.0f, m_lineWidth), 1.0f, true);
}

void RadiantCursorEffect::drawPetals(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int petals = std::clamp(m_count + 3, 5, 12);
    const float spread = m_size * easeOut(progress) * 0.62f;
    const float petalRadius = std::max(3.0f, m_size * (0.12f + progress * 0.12f));
    const float alpha = eventAlpha(progress);
    for (int index = 0; index < petals; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / petals
            - std::numbers::pi_v<float> / 2.0f + layoutRotation(event.variant)
            + (layoutValue(index, event.variant, 0) - 0.5f) * 0.24f;
        const float petalSpread = spread * (0.86f + 0.14f * layoutValue(index, event.variant, 1));
        const QPointF point = event.position + QPointF(std::cos(angle) * petalSpread, std::sin(angle) * petalSpread);
        drawCircle(viewport, withAlpha(color, alpha * 0.82f), point, petalRadius, m_lineWidth);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.22f), event.position, petalRadius * 1.15f, m_lineWidth, true);
}

void RadiantCursorEffect::drawDiamond(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int layers = std::clamp(m_count, 1, 8);
    const float alpha = eventAlpha(progress);
    const QVector2D center(event.position);
    for (int layer = 0; layer < layers; ++layer) {
        const float delay = float(layer) / float(std::max(5, layers * 5));
        if (progress < delay) {
            continue;
        }
        const float local = clamp01((progress - delay) / (1.0f - delay));
        const float radius = m_size * easeOut(local) * (1.0f - layer * 0.035f);
        QList<QVector2D> diamond = {
            center + QVector2D(0, -radius), center + QVector2D(radius, 0),
            center + QVector2D(0, radius), center + QVector2D(-radius, 0),
        };
        drawLines(viewport, withAlpha(color, alpha * (1.0f - layer * 0.08f)), diamond, m_lineWidth, GL_LINE_LOOP);
    }
}

void RadiantCursorEffect::drawSonar(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float alpha = eventAlpha(progress);
    const float direction = -0.8f + progress * 1.6f;
    const QVector2D center(event.position);
    QList<QVector2D> beam = {center, center + QVector2D(std::cos(direction), std::sin(direction)) * (m_size * easeOut(progress))};
    drawLines(viewport, withAlpha(color, alpha), beam, m_lineWidth, GL_LINES);
    const int arcs = std::clamp(m_count, 2, 6);
    for (int arc = 1; arc <= arcs; ++arc) {
        QList<QVector2D> vertices;
        const float radius = m_size * easeOut(progress) * arc / arcs;
        for (int segment = 0; segment <= 24; ++segment) {
            const float angle = direction - 0.78f + 1.56f * segment / 24.0f;
            vertices << center + QVector2D(std::cos(angle), std::sin(angle)) * radius;
        }
        drawLines(viewport, withAlpha(color, alpha * (0.4f + 0.6f * arc / arcs)), vertices, std::max(1.0f, m_lineWidth * 0.7f), GL_LINE_STRIP);
    }
}

void RadiantCursorEffect::drawVortex(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int arms = std::clamp(m_count, 2, 6);
    const float alpha = eventAlpha(progress);
    const QVector2D center(event.position);
    for (int arm = 0; arm < arms; ++arm) {
        QList<QVector2D> spiral;
        for (int segment = 0; segment <= 52; ++segment) {
            const float t = segment / 52.0f;
            const float radius = m_size * easeOut(progress) * t;
            const float angle = arm * 2.0f * std::numbers::pi_v<float> / arms + t * std::numbers::pi_v<float> * 2.4f + progress * 2.2f;
            spiral << center + QVector2D(std::cos(angle), std::sin(angle)) * radius;
        }
        drawLines(viewport, withAlpha(color, alpha * (1.0f - arm * 0.07f)), spiral, std::max(1.0f, m_lineWidth * 0.72f), GL_LINE_STRIP);
    }
}

void RadiantCursorEffect::drawCross(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float radius = m_size * easeOut(progress);
    const float gap = radius * 0.24f;
    const QVector2D center(event.position);
    const float alpha = eventAlpha(progress);
    QList<QVector2D> lines = {
        center + QVector2D(-radius, 0), center + QVector2D(-gap, 0),
        center + QVector2D(gap, 0), center + QVector2D(radius, 0),
        center + QVector2D(0, -radius), center + QVector2D(0, -gap),
        center + QVector2D(0, gap), center + QVector2D(0, radius),
    };
    drawLines(viewport, withAlpha(color, alpha), lines, m_lineWidth, GL_LINES);
    drawCircle(viewport, withAlpha(color, alpha * 0.7f), event.position, gap, std::max(1.0f, m_lineWidth * 0.7f));
}

void RadiantCursorEffect::drawConfetti(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int pieces = std::clamp(m_count * 3, 10, 36);
    const float travel = m_size * easeOut(progress);
    const float alpha = eventAlpha(progress);
    QList<QVector2D> lines;
    for (int index = 0; index < pieces; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / pieces
            + layoutRotation(event.variant) + (layoutValue(index, event.variant, 0) - 0.5f) * 0.62f;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D tangent(-direction.y(), direction.x());
        const QVector2D point(event.position);
        const QVector2D start = point + direction * (travel * (0.38f + 0.62f * layoutValue(index, event.variant, 1)));
        lines << start - tangent * (m_lineWidth + 2.0f) << start + tangent * (m_lineWidth + 2.0f);
    }
    drawLines(viewport, withAlpha(color, alpha), lines, std::max(1.5f, m_lineWidth), GL_LINES);
}

void RadiantCursorEffect::drawLightning(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int bolts = std::clamp(m_count, 3, 9);
    const float alpha = eventAlpha(progress);
    const float length = m_size * easeOut(progress);
    const QVector2D center(event.position);
    for (int bolt = 0; bolt < bolts; ++bolt) {
        const float angle = 2.0f * std::numbers::pi_v<float> * bolt / bolts
            - std::numbers::pi_v<float> / 2.0f + layoutRotation(event.variant)
            + (layoutValue(bolt, event.variant, 0) - 0.5f) * 0.28f;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D tangent(-direction.y(), direction.x());
        QList<QVector2D> zigzag = {
            center + direction * (length * 0.12f),
            center + direction * (length * 0.38f) + tangent * (length * 0.1f),
            center + direction * (length * 0.58f) - tangent * (length * 0.07f),
            center + direction * length,
        };
        drawLines(viewport, withAlpha(color, alpha), zigzag, std::max(1.0f, m_lineWidth * 0.8f), GL_LINE_STRIP);
    }
}

void RadiantCursorEffect::drawBubbles(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int bubbles = std::clamp(m_count + 2, 4, 14);
    const float alpha = eventAlpha(progress);
    const float travel = m_size * easeOut(progress);
    for (int index = 0; index < bubbles; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / bubbles
            - std::numbers::pi_v<float> / 2.0f + layoutRotation(event.variant)
            + (layoutValue(index, event.variant, 0) - 0.5f) * 0.52f;
        const float distance = travel * (0.3f + 0.7f * layoutValue(index, event.variant, 1));
        const QPointF point = event.position + QPointF(std::cos(angle) * distance, std::sin(angle) * distance - progress * m_size * 0.18f);
        const float radius = std::max(2.5f, m_size * (0.045f + 0.045f * layoutValue(index, event.variant, 2)));
        drawCircle(viewport, withAlpha(color, alpha * (0.55f + 0.1f * (index % 4))), point, radius, std::max(1.0f, m_lineWidth * 0.55f));
    }
}

void RadiantCursorEffect::drawHeart(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float scale = m_size * easeOut(progress) / 18.0f;
    const QVector2D center(event.position.x(), event.position.y() - m_size * 0.08f);
    QList<QVector2D> heart;
    for (int index = 0; index < 72; ++index) {
        const float t = 2.0f * std::numbers::pi_v<float> * index / 72.0f;
        const float x = 16.0f * std::pow(std::sin(t), 3.0f);
        const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
        heart << center + QVector2D(x * scale, -y * scale);
    }
    const float alpha = eventAlpha(progress);
    drawLines(viewport, withAlpha(color, alpha), heart, m_lineWidth, GL_LINE_LOOP);
    if (m_glow) {
        drawCircle(viewport, withAlpha(color, alpha * 0.08f), event.position, m_size * easeOut(progress) * 0.9f, 1.0f, true);
    }
}

void RadiantCursorEffect::drawInk(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float alpha = eventAlpha(progress);
    const float radius = m_size * (0.16f + 0.84f * eased);
    const int lobes = std::clamp(m_count + 3, 6, 12);

    drawCircle(viewport, withAlpha(color, alpha * 0.34f), event.position, radius * 0.72f, 1.0f, true);
    for (int index = 0; index < lobes; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / lobes
            + layoutRotation(event.variant) + (layoutValue(index, event.variant, 0) - 0.5f) * 0.4f;
        const float wobble = 0.68f + 0.3f * layoutValue(index, event.variant, 1);
        const QPointF point = event.position + QPointF(std::cos(angle), std::sin(angle)) * (radius * 0.42f * wobble);
        drawCircle(viewport, withAlpha(color, alpha * 0.22f), point, radius * (0.28f + 0.05f * (index % 3)), 1.0f, true);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.68f), event.position, std::max(2.5f, radius * 0.12f), 1.0f, true);
}

void RadiantCursorEffect::drawSplash(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float alpha = eventAlpha(progress);
    const int drops = std::clamp(m_count * 2 + 4, 8, 24);
    drawCircle(viewport, withAlpha(color, alpha * 0.46f), event.position,
               m_size * (0.12f + 0.22f * std::sin(progress * std::numbers::pi_v<float>)), 1.0f, true);

    for (int index = 0; index < drops; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / drops
            + layoutRotation(event.variant) + (layoutValue(index, event.variant, 0) - 0.5f) * 0.64f;
        const float variation = 0.45f + 0.55f * layoutValue(index, event.variant, 1);
        const float distance = m_size * eased * variation;
        const QPointF point = event.position + QPointF(std::cos(angle), std::sin(angle)) * distance;
        const float radius = std::max(2.0f, m_size * (0.03f + 0.045f * layoutValue(index, event.variant, 2)) * (1.0f - progress * 0.3f));
        drawCircle(viewport, withAlpha(color, alpha * (0.48f + 0.1f * (index % 3))), point, radius, 1.0f, true);
    }
}

void RadiantCursorEffect::drawNova(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int points = std::clamp(m_count + 6, 8, 18);
    const float radius = m_size * easeOut(progress);
    const float rotation = progress * 0.85f - std::numbers::pi_v<float> / 2.0f + layoutRotation(event.variant);
    const QVector2D center(event.position);
    QList<QVector2D> star;
    star.reserve(points * 2);
    for (int index = 0; index < points * 2; ++index) {
        const float angle = rotation + std::numbers::pi_v<float> * index / points;
        const float pointRadius = index % 2 == 0 ? radius : radius * (0.24f + 0.08f * progress);
        star << center + QVector2D(std::cos(angle), std::sin(angle)) * pointRadius;
    }
    const float alpha = eventAlpha(progress);
    drawPolygon(viewport, withAlpha(color, alpha * 0.34f), star);
    drawCircle(viewport, withAlpha(color, alpha * 0.9f), event.position, std::max(3.0f, radius * 0.18f), 1.0f, true);
    drawCircle(viewport, withAlpha(color, alpha * 0.32f), event.position, radius * 0.42f, 1.0f, true);
}

void RadiantCursorEffect::drawComet(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float alpha = eventAlpha(progress);
    const float travel = m_size * easeOut(progress) * 0.66f;
    const float angle = -2.35f + progress * 1.35f + layoutRotation(event.variant);
    const QVector2D direction(std::cos(angle), std::sin(angle));
    const QVector2D tangent(-direction.y(), direction.x());
    const QVector2D center(event.position);
    const QVector2D head = center + direction * travel;

    for (int tail = 0; tail < 3; ++tail) {
        const float width = m_size * (0.17f - tail * 0.035f);
        const float length = m_size * (0.72f + tail * 0.18f) * easeOut(progress);
        const float offset = (tail - 1) * m_size * 0.08f;
        QList<QVector2D> flame = {
            head + tangent * (width + offset),
            head - direction * length + tangent * offset,
            head - tangent * (width - offset),
        };
        drawPolygon(viewport, withAlpha(color, alpha * (0.16f + 0.08f * (2 - tail))), flame);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.9f), head.toPointF(), std::max(3.0f, m_size * 0.2f), 1.0f, true);
    drawCircle(viewport, withAlpha(color, alpha * 0.26f), head.toPointF(), m_size * 0.34f, 1.0f, true);
}

void RadiantCursorEffect::drawEclipse(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const float eased = easeOut(progress);
    const float alpha = eventAlpha(progress);
    const float radius = m_size * (0.18f + 0.66f * eased);
    drawCircle(viewport, withAlpha(color, alpha * 0.2f), event.position, radius, 1.0f, true);
    drawCircle(viewport, withAlpha(color, alpha * 0.82f), event.position, radius * 0.48f, 1.0f, true);

    const float angle = progress * std::numbers::pi_v<float> * 2.2f - 0.8f + layoutRotation(event.variant);
    const QPointF moon = event.position + QPointF(std::cos(angle), std::sin(angle)) * radius * 0.78f;
    drawCircle(viewport, withAlpha(color.lighter(135), alpha * 0.72f), moon, radius * 0.24f, 1.0f, true);
    drawCircle(viewport, withAlpha(color, alpha * 0.48f), event.position, radius * 1.15f, std::max(1.0f, m_lineWidth * 0.65f));
}

void RadiantCursorEffect::drawPlasma(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int orbs = std::clamp(m_count + 4, 7, 16);
    const float alpha = eventAlpha(progress);
    const float spread = m_size * easeOut(progress) * 0.76f;
    for (int index = 0; index < orbs; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / orbs
            + progress * (1.2f + 0.08f * index) + layoutRotation(event.variant)
            + (layoutValue(index, event.variant, 0) - 0.5f) * 0.46f;
        const float orbit = spread * (0.24f + 0.72f * layoutValue(index, event.variant, 1));
        const QPointF point = event.position + QPointF(std::cos(angle) * orbit,
                                                       std::sin(angle) * orbit * 0.72f);
        const float radius = std::max(2.5f, m_size * (0.055f + 0.055f * layoutValue(index, event.variant, 2)));
        drawCircle(viewport, withAlpha(color, alpha * (0.24f + 0.11f * (index % 4))), point, radius, 1.0f, true);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.56f), event.position, std::max(3.0f, m_size * 0.16f), 1.0f, true);
}

void RadiantCursorEffect::drawPixelBurst(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int pixels = std::clamp(m_count * 3, 9, 30);
    const float alpha = eventAlpha(progress);
    const float travel = m_size * easeOut(progress);
    const QVector2D center(event.position);
    for (int index = 0; index < pixels; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / pixels
            + layoutRotation(event.variant) + (layoutValue(index, event.variant, 0) - 0.5f) * 0.76f;
        const float distance = travel * (0.28f + 0.72f * layoutValue(index, event.variant, 1));
        const QVector2D point = center + QVector2D(std::cos(angle), std::sin(angle)) * distance;
        const float half = std::max(2.0f, m_size * (0.03f + 0.045f * layoutValue(index, event.variant, 2)));
        QList<QVector2D> square = {
            point + QVector2D(-half, -half), point + QVector2D(half, -half),
            point + QVector2D(half, half), point + QVector2D(-half, half),
        };
        drawPolygon(viewport, withAlpha(color, alpha * (0.46f + 0.12f * (index % 3))), square);
    }
}

void RadiantCursorEffect::drawPrism(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int shards = std::clamp(m_count * 2 + 3, 7, 21);
    const float alpha = eventAlpha(progress);
    const float radius = m_size * easeOut(progress);
    const QVector2D center(event.position);
    for (int index = 0; index < shards; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / shards
            + progress * 0.5f + layoutRotation(event.variant)
            + (layoutValue(index, event.variant, 0) - 0.5f) * 0.38f;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D tangent(-direction.y(), direction.x());
        const float inner = radius * (0.18f + 0.08f * (index % 3));
        const float outer = radius * (0.62f + 0.38f * layoutValue(index, event.variant, 1));
        const float width = m_size * (0.07f + 0.018f * (index % 3));
        QList<QVector2D> triangle = {
            center + direction * inner - tangent * width,
            center + direction * outer,
            center + direction * inner + tangent * width,
        };
        drawPolygon(viewport, withAlpha(index % 2 ? color.lighter(125) : color,
                                        alpha * (0.28f + 0.12f * (index % 3))), triangle);
    }
}

void RadiantCursorEffect::drawFlower(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int petals = std::clamp(m_count + 4, 6, 14);
    const float alpha = eventAlpha(progress);
    const float spread = m_size * easeOut(progress) * 0.52f;
    const float rotation = progress * 1.4f + layoutRotation(event.variant);
    const float petalRadius = std::max(3.0f, m_size * (0.16f + 0.04f * std::sin(progress * std::numbers::pi_v<float>)));
    for (int index = 0; index < petals; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / petals + rotation;
        const QPointF point = event.position + QPointF(std::cos(angle), std::sin(angle)) * spread;
        drawCircle(viewport, withAlpha(color, alpha * 0.3f), point, petalRadius * 1.3f, 1.0f, true);
        drawCircle(viewport, withAlpha(color, alpha * 0.68f), point, petalRadius, 1.0f, true);
    }
    drawCircle(viewport, withAlpha(color.lighter(130), alpha * 0.88f), event.position, petalRadius * 0.82f, 1.0f, true);
}

void RadiantCursorEffect::drawMeteor(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress)
{
    const int spikes = std::clamp(m_count * 2 + 4, 8, 24);
    const float alpha = eventAlpha(progress);
    const float radius = m_size * easeOut(progress);
    const QVector2D center(event.position);
    for (int index = 0; index < spikes; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / spikes
            + layoutRotation(event.variant) + (layoutValue(index, event.variant, 0) - 0.5f) * 0.42f;
        const QVector2D direction(std::cos(angle), std::sin(angle));
        const QVector2D tangent(-direction.y(), direction.x());
        const float length = radius * (0.56f + 0.44f * layoutValue(index, event.variant, 1));
        const float base = radius * 0.24f;
        const float width = m_size * (0.045f + 0.015f * (index % 3));
        QList<QVector2D> shard = {
            center + direction * base - tangent * width,
            center + direction * length,
            center + direction * base + tangent * width,
        };
        drawPolygon(viewport, withAlpha(color, alpha * (0.2f + 0.12f * (index % 3))), shard);
    }
    drawCircle(viewport, withAlpha(color, alpha * 0.32f), event.position, radius * 0.34f, 1.0f, true);
    drawCircle(viewport, withAlpha(color.lighter(145), alpha * 0.92f), event.position, std::max(3.0f, radius * 0.13f), 1.0f, true);
}

void RadiantCursorEffect::drawPolygon(const RenderViewport &viewport, const QColor &color, const QList<QVector2D> &perimeter)
{
    if (perimeter.size() < 3) {
        return;
    }
    QVector2D center;
    for (const QVector2D &point : perimeter) {
        center += point;
    }
    center /= float(perimeter.size());

    QList<QVector2D> fan;
    fan.reserve(perimeter.size() + 2);
    fan << center;
    fan.append(perimeter);
    fan << perimeter.front();
    drawLines(viewport, color, fan, 1.0f, GL_TRIANGLE_FAN);
}

void RadiantCursorEffect::drawCircle(const RenderViewport &viewport, const QColor &color, const QPointF &center, float radius, float width, bool filled)
{
    QList<QVector2D> vertices;
    vertices.reserve(CircleSegments + (filled ? 2 : 0));
    if (filled) {
        vertices << QVector2D(center);
    }
    for (int index = 0; index < CircleSegments; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * index / CircleSegments;
        vertices << QVector2D(center.x() + std::cos(angle) * radius,
                              center.y() + std::sin(angle) * radius);
    }
    if (filled) {
        vertices << vertices[1];
    }
    drawLines(viewport, color, vertices, width, filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
}

void RadiantCursorEffect::drawLines(const RenderViewport &viewport, const QColor &color, const QList<QVector2D> &vertices, float width, unsigned int primitiveMode)
{
    if (vertices.isEmpty()) {
        return;
    }
    if (effects->isOpenGLCompositing()) {
        QList<QVector2D> scaled;
        scaled.reserve(vertices.size());
        for (const QVector2D &vertex : vertices) {
            scaled << vertex * viewport.scale();
        }
        GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setVertices(scaled);
        GLShader *shader = ShaderManager::instance()->getBoundShader();
        const bool glowEnabled = m_drawingTrail ? m_trailGlow : m_glow;
        if (glowEnabled && primitiveMode != GL_TRIANGLE_FAN) {
            shader->setUniform(GLShader::ColorUniform::Color, withAlpha(color, color.alphaF() * 0.2f));
            glLineWidth(std::max(2.0f, width * 3.2f));
            vbo->render(primitiveMode);
        }
        shader->setUniform(GLShader::ColorUniform::Color, color);
        glLineWidth(std::max(1.0f, width));
        vbo->render(primitiveMode);
    } else if (effects->compositingType() == QPainterCompositing) {
        QPainter *painter = effects->scenePainter();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        QPolygonF polygon;
        for (const QVector2D &vertex : vertices) {
            polygon << vertex.toPointF();
        }
        if (primitiveMode == GL_TRIANGLE_FAN) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPolygon(polygon);
        } else if (primitiveMode == GL_LINE_LOOP) {
            painter->drawPolygon(polygon);
        } else if (primitiveMode == GL_LINE_STRIP) {
            painter->drawPolyline(polygon);
        } else {
            for (int index = 0; index + 1 < polygon.size(); index += 2) {
                painter->drawLine(polygon[index], polygon[index + 1]);
            }
        }
        painter->restore();
    }
}

void RadiantCursorEffect::drawLabel(ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport, float alpha)
{
    if (!event.labelTexture) {
        const QString names[] = {QStringLiteral("Esquerdo"), QStringLiteral("Meio"), QStringLiteral("Direito")};
        const QString text = names[event.button] + (event.pressed ? QStringLiteral(" ↓") : QStringLiteral(" ↑"));
        const QFontMetrics metrics(m_font);
        const QRect bounds = metrics.boundingRect(text).adjusted(-7, -4, 7, 4);
        QImage image(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        painter.setFont(m_font);
        painter.drawText(image.rect(), Qt::AlignCenter, text);
        painter.end();
        event.labelSize = image.size();
        event.labelTexture = GLTexture::upload(image);
        if (event.labelTexture) {
            event.labelTexture->setFilter(GL_LINEAR);
            event.labelTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        }
    }
    if (!event.labelTexture) {
        return;
    }

    const QSizeF pixelSize = QSizeF(event.labelSize) * viewport.scale();
    const QPointF pixelPosition = (event.position + QPointF(m_size + 8, -event.labelSize.height() / 2.0)) * viewport.scale();
    GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::TransformColorspace);
    QMatrix4x4 matrix = viewport.projectionMatrix();
    matrix.translate(pixelPosition.x(), pixelPosition.y());
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1.0f, 1.0f, 1.0f, alpha));
    shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
    event.labelTexture->render(pixelSize);
    ShaderManager::instance()->popShader();
}

void RadiantCursorEffect::repaintEvents()
{
    Region currentDamage;
    for (const ClickEvent &event : m_events) {
        const int baseRadius = event.program
            ? int(std::ceil(event.program->maximumBounds))
            : m_size * 2 + int(std::ceil(m_lineWidth * 4.0f)) + 32;
        const int radius = baseRadius * 2;
        currentDamage += QRect(int(event.position.x()) - radius, int(event.position.y()) - radius,
                               radius * 2 + 220, radius * 2);
    }
    const int trailRadius = (int(std::ceil(m_trailSize * 5.0f)) + 12) * 2;
    for (const TrailPoint &point : m_trailPoints) {
        currentDamage += QRect(int(point.position.x()) - trailRadius,
                               int(point.position.y()) - trailRadius,
                               trailRadius * 2, trailRadius * 2);
    }

    Region dirty = m_previousDamage;
    dirty += currentDamage;
    m_previousDamage = currentDamage;
    if (!dirty.isEmpty()) {
        effects->addRepaint(dirty);
    }
}

float RadiantCursorEffect::easeOut(float progress)
{
    const float inverse = 1.0f - clamp01(progress);
    return 1.0f - inverse * inverse * inverse;
}

float RadiantCursorEffect::eventAlpha(float progress)
{
    const float inverse = 1.0f - clamp01(progress);
    return inverse * inverse;
}

bool RadiantCursorEffect::buttonPressed(Qt::MouseButton button, Qt::MouseButtons buttons, Qt::MouseButtons oldButtons)
{
    return buttons.testFlag(button) && !oldButtons.testFlag(button);
}

bool RadiantCursorEffect::buttonReleased(Qt::MouseButton button, Qt::MouseButtons buttons, Qt::MouseButtons oldButtons)
{
    return !buttons.testFlag(button) && oldButtons.testFlag(button);
}

} // namespace KWin

#include "moc_radiantcursoreffect.cpp"
