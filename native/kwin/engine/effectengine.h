// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector2D>

#include <memory>

namespace KWin::RadiantCursorEngine
{

enum class Easing : unsigned char {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutBack,
    EaseOutBounce,
    EaseOutElastic,
};

template<typename T>
struct Keyframe {
    float time = 0.0f;
    T value{};
    Easing easing = Easing::Linear;
};

template<typename T>
struct Track {
    bool animated = false;
    T constant{};
    QVector<Keyframe<T>> frames;
};

enum class GeometryKind : unsigned char {
    Circle,
    Ring,
    Rect,
    Line,
    Polygon,
    Star,
    Diamond,
};

enum class LayerKind : unsigned char {
    Shape,
    Particles,
    Image,
};

enum class BlendMode : unsigned char {
    Normal,
    Additive,
};

struct CompiledGeometry {
    GeometryKind kind = GeometryKind::Circle;
    float radius = 20.0f;
    QVector2D size = QVector2D(40.0f, 40.0f);
    QVector2D lineStart;
    QVector2D lineEnd;
    float innerRadius = 8.0f;
    int points = 5;
};

struct CompiledMaterial {
    QColor fillColor = Qt::transparent;
    QColor strokeColor = Qt::white;
    bool fillUsesButtonColor = false;
    bool strokeUsesButtonColor = true;
    bool hasFill = false;
    bool hasStroke = true;
    Track<float> strokeWidth;
    Track<float> opacity;
    BlendMode blendMode = BlendMode::Normal;
    float glow = 0.0f;
};

struct CompiledTransform {
    Track<QVector2D> position;
    Track<QVector2D> scale;
    Track<float> rotationDegrees;
};

struct CompiledEmitter {
    enum class Mode : unsigned char { Radial, Cone, Point };
    enum class Distribution : unsigned char { Even, Random };
    Mode mode = Mode::Radial;
    Distribution distribution = Distribution::Random;
    int count = 1;
    float angleDegrees = 0.0f;
    float spreadDegrees = 360.0f;
    float speed = 100.0f;
    float speedVariation = 0.0f;
    float spawnRadius = 0.0f;
    QVector2D gravity;
    float drag = 0.0f;
    int variants = 1;
};

struct CompiledLayer {
    QString id;
    LayerKind kind = LayerKind::Shape;
    bool enabled = true;
    int startMs = 0;
    int durationMs = 500;
    CompiledTransform transform;
    CompiledGeometry geometry;
    CompiledMaterial material;
    CompiledEmitter emitter;
    Track<float> particleScale;
    Track<float> particleRotation;
    std::shared_ptr<const QImage> image;
    QString imageKey;
    QVector2D imageSize;
    // Motion runtime v2. Groups are transform-only nodes; shapes inherit these values.
    int parentIndex = -1;
    bool motionNode = false;
    bool groupNode = false;
    float baseOpacity = 1.0f;
    enum class MotionProperty : unsigned char { PositionX, PositionY, Rotation, ScaleX, ScaleY, Opacity };
    struct MotionChannel {
        MotionProperty property = MotionProperty::PositionX;
        bool multiply = false;
        QVector<Keyframe<float>> frames; // absolute milliseconds in the element's local time
    };
    QVector<MotionChannel> motionChannels;
};

struct Diagnostic {
    enum class Severity : unsigned char { Warning, Error };
    Severity severity = Severity::Error;
    QString code;
    QString path;
    QString message;
};

struct RenderCommand {
    enum class Kind : unsigned char { Circle, Polygon, Lines, Image };
    Kind kind = Kind::Circle;
    QPointF center;
    float radius = 1.0f;
    float width = 1.0f;
    bool filled = false;
    QColor color = Qt::white;
    BlendMode blendMode = BlendMode::Normal;
    QVector<QVector2D> points;
    std::shared_ptr<const QImage> image;
    QString imageKey;
    QSizeF imageSize;
    float rotationRadians = 0.0f;
};

class CompiledEffect
{
public:
    QString id;
    QString revision;
    int durationMs = 500;
    float maximumBounds = 256.0f;
    QVector<CompiledLayer> layers;

    void evaluate(int elapsedMs, const QPointF &origin, const QColor &buttonColor,
                  int variant, QVector<RenderCommand> &commands) const;
};

struct LoadResult {
    std::shared_ptr<const CompiledEffect> effect;
    QVector<Diagnostic> diagnostics;
};

class EffectLoader
{
public:
    static LoadResult load(const QString &effectId, const QString &revision);
};

float evaluateEasing(Easing easing, float progress);

} // namespace KWin::RadiantCursorEngine
