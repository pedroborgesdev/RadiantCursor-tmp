// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "effect/effect.h"
#include "engine/effectengine.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector2D>

#include <chrono>
#include <deque>
#include <memory>

namespace KWin
{

class GLTexture;
class RenderTarget;
class RenderViewport;

class RadiantCursorEffect : public Effect
{
    Q_OBJECT

public:
    RadiantCursorEffect();
    ~RadiantCursorEffect() override;

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &deviceRegion, LogicalOutput *screen) override;
    void postPaintScreen() override;
    bool isActive() const override;

private Q_SLOTS:
    void handleMouseChanged(const QPointF &position, const QPointF &oldPosition,
                            Qt::MouseButtons buttons, Qt::MouseButtons oldButtons,
                            Qt::KeyboardModifiers modifiers, Qt::KeyboardModifiers oldModifiers);

private:
    enum class Trigger {
        Press,
        Release,
        Both,
    };

    struct ClickEvent {
        int button = 0;
        QPointF position;
        int elapsed = 0;
        bool pressed = true;
        int variant = 0;
        int lifetime = 500;
        std::shared_ptr<const RadiantCursorEngine::CompiledEffect> program;
        std::unique_ptr<GLTexture> labelTexture;
        QSize labelSize;
    };

    struct TrailPoint {
        QPointF position;
        QVector2D direction;
        float scale = 1.0f;
        int elapsed = 0;
        int variant = 0;
        unsigned int serial = 0;
    };

    void drawEvent(const ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport);
    void drawDeclarativeEvent(const ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport);
    void drawTrailPoint(const TrailPoint &point, const RenderViewport &viewport);
    void drawHalo(const RenderTarget &renderTarget, const RenderViewport &viewport);
    void drawRipple(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawPulse(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawTarget(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawBurst(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawSpark(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawFocus(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawHalo(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawShockwave(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawOrbit(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawPetals(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawDiamond(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawSonar(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawVortex(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawCross(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawConfetti(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawLightning(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawBubbles(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawHeart(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawInk(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawSplash(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawNova(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawComet(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawEclipse(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawPlasma(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawPixelBurst(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawPrism(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawFlower(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);
    void drawMeteor(const ClickEvent &event, const RenderViewport &viewport, const QColor &color, float progress);

    void drawCircle(const RenderViewport &viewport, const QColor &color, const QPointF &center, float radius, float width, bool filled = false);
    void drawPolygon(const RenderViewport &viewport, const QColor &color, const QList<QVector2D> &perimeter);
    void drawLines(const RenderViewport &viewport, const QColor &color, const QList<QVector2D> &vertices, float width, unsigned int primitiveMode);
    void drawLabel(ClickEvent &event, const RenderTarget &renderTarget, const RenderViewport &viewport, float alpha);
    void repaintEvents();
    QPointF currentCursorCenterOffset() const;

    static float easeOut(float progress);
    static float eventAlpha(float progress);
    static bool buttonPressed(Qt::MouseButton button, Qt::MouseButtons buttons, Qt::MouseButtons oldButtons);
    static bool buttonReleased(Qt::MouseButton button, Qt::MouseButtons buttons, Qt::MouseButtons oldButtons);

    QColor m_colors[3];
    float m_lineWidth = 2.0f;
    int m_life = 520;
    int m_size = 54;
    int m_count = 3;
    bool m_showText = false;
    bool m_glow = true;
    bool m_clickEnabled = true;
    bool m_trailEnabled = false;
    bool m_trailGlow = true;
    bool m_trailOnlyPressed = false;
    bool m_drawingTrail = false;
    bool m_drawingHalo = false;
    QColor m_trailColor = Qt::white;
    QString m_trailStyle = QStringLiteral("dots");
    float m_trailSize = 14.0f;
    int m_trailLife = 520;
    int m_trailDensity = 65;
    int m_trailFrequency = 30;
    float m_trailOpacity = 0.72f;
    QPointF m_cursorCenterOffset{8.0, 8.0};
    QPointF m_cursorTextOffset{8.0, 8.0};
    QPointF m_cursorLinkOffset{8.0, 8.0};
    QPointF m_cursorCrosshairOffset{8.0, 8.0};
    QPointF m_cursorBusyOffset{8.0, 8.0};
    QPointF m_cursorMoveOffset{8.0, 8.0};
    QPointF m_cursorForbiddenOffset{8.0, 8.0};
    QPointF m_cursorHelpOffset{8.0, 8.0};
    QPointF m_cursorResizeHorizontalOffset{8.0, 8.0};
    QPointF m_cursorResizeVerticalOffset{8.0, 8.0};
    QPointF m_cursorResizeDiagonalNwSeOffset{8.0, 8.0};
    QPointF m_cursorResizeDiagonalNeSwOffset{8.0, 8.0};
    float m_trailDistance = 0.0f;
    bool m_haloEnabled = false;
    bool m_haloGlow = true;
    bool m_haloCycleVariants = true;
    QColor m_haloColor = QColor(QStringLiteral("#8bd97b"));
    QString m_haloStyle = QStringLiteral("orbitTrail");
    float m_haloSize = 18.0f;
    float m_haloDistance = 48.0f;
    int m_haloDensity = 55;
    float m_haloOpacity = 0.82f;
    float m_haloSpeed = 1.0f;
    int m_haloVariantInterval = 1400;
    QPointF m_cursorPosition;
    QFont m_font;
    QString m_style = QStringLiteral("ripple");
    Trigger m_trigger = Trigger::Press;
    unsigned int m_eventSequence = 0;
    unsigned int m_trailSequence = 0;
    qint64 m_lastTrailEmission = -1000;
    QPointF m_lastTrailEmissionPosition;
    bool m_hasLastTrailEmissionPosition = false;
    QElapsedTimer m_trailEmissionTimer;
    std::chrono::milliseconds m_lastPresentTime = std::chrono::milliseconds::zero();
    std::deque<ClickEvent> m_events;
    std::deque<TrailPoint> m_trailPoints;
    std::shared_ptr<const RadiantCursorEngine::CompiledEffect> m_program;
    std::shared_ptr<const RadiantCursorEngine::CompiledEffect> m_haloProgram;
    QVector<RadiantCursorEngine::Diagnostic> m_programDiagnostics;
    QVector<RadiantCursorEngine::RenderCommand> m_renderCommands;
    QHash<QString, std::shared_ptr<GLTexture>> m_imageTextures;
    Region m_previousDamage;
};

} // namespace KWin
