// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rc {

struct Vec2 { float x = 0.0f; float y = 0.0f; };
struct Color { float r = 1.0f; float g = 1.0f; float b = 1.0f; float a = 1.0f; };

struct Settings {
    bool clickEnabled = true;
    Color colors[3]{{1,0,0,1},{0,1,0,1},{0,0,1,1}};
    float lineWidth = 2.0f;
    int lifeMs = 520;
    float size = 54.0f;
    int count = 3;
    bool showText = false;
    std::string font = "Segoe UI";
    std::string style = "ripple";
    std::string trigger = "press";
    bool glow = true;
    bool trailEnabled = false;
    std::string trailStyle = "dots";
    Color trailColor{1,1,1,1};
    float trailSize = 14.0f;
    int trailLifeMs = 520;
    int trailDensity = 65;
    int trailFrequency = 30;
    float trailOpacity = 0.72f;
    float trailOffsetX = 8.0f;
    float trailOffsetY = 8.0f;
    float trailDistance = 0.0f;
    bool trailGlow = true;
    bool trailOnlyPressed = false;
};

namespace RadiantCursorEngine {

enum class Easing : unsigned char {
    Linear, EaseInQuad, EaseOutQuad, EaseInOutQuad, EaseInCubic,
    EaseOutCubic, EaseInOutCubic, EaseOutBack, EaseOutBounce, EaseOutElastic,
};

struct Keyframe { float time = 0.0f; float value = 0.0f; Easing easing = Easing::Linear; };
enum class GeometryKind : unsigned char { Circle, Ring, Rect, Line, Polygon, Star, Diamond };
enum class BlendMode : unsigned char { Normal, Additive };

struct CompiledGeometry {
    GeometryKind kind = GeometryKind::Circle;
    float radius = 20.0f;
    Vec2 size{40,40};
    Vec2 lineStart;
    Vec2 lineEnd;
    float innerRadius = 8.0f;
    int points = 5;
};

struct CompiledMaterial {
    Color fillColor{1,1,1,0};
    Color strokeColor{1,1,1,1};
    bool hasFill = false;
    bool hasStroke = true;
    float strokeWidth = 2.0f;
    float opacity = 1.0f;
    BlendMode blendMode = BlendMode::Normal;
};

struct CompiledLayer {
    enum class MotionProperty : unsigned char { PositionX, PositionY, Rotation, ScaleX, ScaleY, Opacity };
    struct MotionChannel { MotionProperty property = MotionProperty::PositionX; bool multiply = false; std::vector<Keyframe> frames; };

    std::string id;
    bool enabled = true;
    bool groupNode = false;
    int parentIndex = -1;
    int startMs = 0;
    int durationMs = 500;
    Vec2 position;
    Vec2 scale{1,1};
    float rotationDegrees = 0.0f;
    float baseOpacity = 1.0f;
    CompiledGeometry geometry;
    CompiledMaterial material;
    std::vector<MotionChannel> motionChannels;
};

struct RenderCommand {
    enum class Kind : unsigned char { Circle, Polygon, Lines };
    Kind kind = Kind::Circle;
    Vec2 center;
    float radius = 1.0f;
    float width = 1.0f;
    bool filled = false;
    Color color;
    BlendMode blendMode = BlendMode::Normal;
    std::vector<Vec2> points;
};

class CompiledEffect {
public:
    std::string id;
    std::string revision;
    int durationMs = 500;
    float maximumBounds = 256.0f;
    std::vector<CompiledLayer> layers;

    void evaluate(int elapsedMs, Vec2 origin, Color buttonColor, int variant,
                  std::vector<RenderCommand> &commands) const;
};

float evaluateEasing(Easing easing, float progress);

} // namespace RadiantCursorEngine

struct RuntimeConfiguration {
    bool enabled = false;
    Settings settings;
    std::string activeEffectId;
    std::string activeRevision;
    std::optional<RadiantCursorEngine::CompiledEffect> program;
};

RuntimeConfiguration loadConfiguration(const std::filesystem::path &dataDirectory);
float deterministicRandom(int index, int variant, int channel);

} // namespace rc
