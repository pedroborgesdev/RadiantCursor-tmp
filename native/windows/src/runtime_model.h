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
    bool trailGlow = true;
    bool trailOnlyPressed = false;
};

struct Keyframe { float time = 0.0f; float value = 0.0f; std::string easing = "linear"; };
struct Channel { std::string property; bool multiply = false; std::vector<Keyframe> frames; };
enum class ShapeKind { Circle, Rectangle, Line, Polygon, Star, Diamond };

struct RuntimeNode {
    std::string id;
    bool visible = true;
    bool group = false;
    int parentIndex = -1;
    int startMs = 0;
    int durationMs = 500;
    Vec2 position;
    Vec2 size{40,40};
    Vec2 scale{1,1};
    float rotation = 0;
    float opacity = 1;
    ShapeKind shape = ShapeKind::Circle;
    int sides = 5;
    int points = 5;
    float innerRatio = .45f;
    bool fillEnabled = false;
    Color fill;
    bool strokeEnabled = true;
    Color stroke;
    float strokeWidth = 2;
    bool additive = false;
    std::vector<Channel> channels;
};

struct RuntimeProgram {
    std::string effectId;
    std::string revision;
    int durationMs = 500;
    float maximumBounds = 256;
    std::vector<RuntimeNode> nodes;
};

struct RuntimeConfiguration {
    bool enabled = false;
    Settings settings;
    std::string activeEffectId;
    std::string activeRevision;
    std::optional<RuntimeProgram> program;
};

struct DrawShape {
    ShapeKind kind = ShapeKind::Circle;
    Vec2 center;
    Vec2 size{20,20};
    float rotation = 0;
    int sides = 5;
    int points = 5;
    float innerRatio = .45f;
    bool fillEnabled = false;
    Color fill;
    bool strokeEnabled = true;
    Color stroke;
    float strokeWidth = 2;
    bool additive = false;
};

RuntimeConfiguration loadConfiguration(const std::filesystem::path &dataDirectory);
std::vector<DrawShape> evaluateProgram(const RuntimeProgram &program, int elapsedMs, Vec2 origin);
float evaluateEasing(std::string_view easing, float value);
float deterministicRandom(int index, int variant, int channel);

} // namespace rc
