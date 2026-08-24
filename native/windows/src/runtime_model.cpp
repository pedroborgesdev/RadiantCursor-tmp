#include "runtime_model.h"
#include "json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace rc {
namespace {
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

Color color(std::string_view text, Color fallback = {}) {
    if (text.size() != 7 || text[0] != '#') return fallback;
    auto component = [&](std::size_t offset) -> std::optional<int> {
        int value = 0;
        for (std::size_t i = offset; i < offset + 2; ++i) {
            value <<= 4; const char c = text[i];
            if (c >= '0' && c <= '9') value += c - '0';
            else if (c >= 'a' && c <= 'f') value += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value += c - 'A' + 10;
            else return std::nullopt;
        }
        return value;
    };
    const auto r = component(1), g = component(3), b = component(5);
    if (!r || !g || !b) return fallback;
    return {*r / 255.0f, *g / 255.0f, *b / 255.0f, 1.0f};
}

std::string text(const json::Value &value, std::string fallback = {}) { return value.isString() ? std::string(value.string()) : fallback; }

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
    result.trailGlow = source.at("TrailGlow").boolean(true);
    result.trailOnlyPressed = source.at("TrailOnlyPressed").boolean(false);
    return result;
}

RuntimeProgram parseProgram(const json::Value &root, std::string_view expectedId, std::string_view expectedRevision) {
    RuntimeProgram program;
    if (static_cast<int>(root.at("runtimeVersion").number()) != 1) throw std::runtime_error("unsupported runtime version");
    program.effectId = text(root.at("effectId")); program.revision = text(root.at("revision"));
    if (program.effectId != expectedId || program.revision != expectedRevision) throw std::runtime_error("runtime identity mismatch");
    program.durationMs = clampNumber(root.at("durationMs"), 500, 1, 10000);
    program.maximumBounds = clampNumber(root.at("maxRadius"), 256.0f, 32.0f, 4096.0f);
    const auto &nodes = root.at("nodes").array();
    if (nodes.empty() || nodes.size() > 128) throw std::runtime_error("invalid runtime node count");
    program.nodes.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &source = nodes[index]; RuntimeNode node;
        node.id = text(source.at("id")); node.visible = source.at("visible").boolean(true); node.group = source.at("kind").string() == "group";
        node.parentIndex = static_cast<int>(source.at("parentIndex").number(-1));
        node.startMs = clampNumber(source.at("startMs"), 0, 0, 10000); node.durationMs = clampNumber(source.at("durationMs"), 500, 1, 10000);
        node.opacity = clampNumber(source.at("opacity"), 1.0f, 0.0f, 1.0f);
        if (node.id.empty() || node.parentIndex >= static_cast<int>(index) || node.parentIndex < -1) throw std::runtime_error("invalid runtime hierarchy");
        const auto &transform = source.at("transform");
        node.position = vec2(transform.at("position")); node.size = vec2(transform.at("size"), {40,40}); node.scale = vec2(transform.at("scale"), {1,1}); node.rotation = static_cast<float>(transform.at("rotationDeg").number());
        for (const auto &channelSource : source.at("channels").array()) {
            Channel channel; channel.property = text(channelSource.at("property")); channel.multiply = channelSource.at("composition").string() == "multiply";
            const auto &frames = channelSource.at("keyframes").array(); if (frames.size() < 2 || frames.size() > 64) throw std::runtime_error("invalid keyframe count");
            float previous = -1;
            for (const auto &frame : frames) { Keyframe key; key.time = static_cast<float>(frame.at("time").number(-1)); key.value = static_cast<float>(frame.at("value").number()); key.easing = text(frame.at("easing"), "linear"); if (key.time < previous || key.time < 0 || key.time > node.durationMs) throw std::runtime_error("invalid keyframe time"); previous = key.time; channel.frames.push_back(std::move(key)); }
            node.channels.push_back(std::move(channel));
        }
        if (!node.group) {
            const auto &shape = source.at("shape"); const auto kind = shape.at("kind").string("circle");
            if (kind == "rectangle") node.shape = ShapeKind::Rectangle; else if (kind == "line") node.shape = ShapeKind::Line; else if (kind == "diamond") node.shape = ShapeKind::Diamond; else if (kind == "star") node.shape = ShapeKind::Star; else if (kind == "triangle" || kind == "hexagon" || kind == "polygon") node.shape = ShapeKind::Polygon; else node.shape = ShapeKind::Circle;
            node.sides = kind == "triangle" ? 3 : kind == "hexagon" ? 6 : clampNumber(shape.at("sides"), 5, 3, 32);
            node.points = clampNumber(shape.at("points"), 5, 2, 32); node.innerRatio = clampNumber(shape.at("innerRatio"), .45f, .05f, .95f);
            const auto &appearance = source.at("appearance"), &fill = appearance.at("fill"), &stroke = appearance.at("stroke");
            node.fillEnabled = fill.at("enabled").boolean(false); node.fill = color(fill.at("color").string("#ffffff"));
            node.strokeEnabled = stroke.at("enabled").boolean(true); node.stroke = color(stroke.at("color").string("#ffffff")); node.strokeWidth = clampNumber(stroke.at("width"), 2.0f, .5f, 100.0f);
            const float appearanceOpacity = clampNumber(appearance.at("opacity"), 1.0f, 0.0f, 1.0f); node.fill.a *= appearanceOpacity; node.stroke.a *= appearanceOpacity;
            node.additive = appearance.at("blendMode").string() == "additive";
        }
        program.nodes.push_back(std::move(node));
    }
    return program;
}

float channelValue(const Channel &channel, float time) {
    if (channel.frames.empty()) return channel.multiply ? 1.0f : 0.0f;
    if (time <= channel.frames.front().time) return channel.frames.front().value;
    for (std::size_t index = 1; index < channel.frames.size(); ++index) {
        const auto &a = channel.frames[index - 1], &b = channel.frames[index];
        if (time <= b.time) { const float amount = evaluateEasing(a.easing, (time - a.time) / std::max(.001f, b.time - a.time)); return std::lerp(a.value, b.value, amount); }
    }
    return channel.frames.back().value;
}
}

float evaluateEasing(std::string_view easing, float value) {
    const float x = std::clamp(value, 0.0f, 1.0f);
    if (easing == "easeInQuad") return x*x;
    if (easing == "easeOutQuad") return 1-(1-x)*(1-x);
    if (easing == "easeInOutQuad") return x<.5f?2*x*x:1-std::pow(-2*x+2,2)/2;
    if (easing == "easeInCubic") return x*x*x;
    if (easing == "easeOutCubic") return 1-std::pow(1-x,3);
    if (easing == "easeInOutCubic") return x<.5f?4*x*x*x:1-std::pow(-2*x+2,3)/2;
    if (easing == "easeOutBack") { constexpr float c1=1.70158f,c3=c1+1; return 1+c3*std::pow(x-1,3)+c1*std::pow(x-1,2); }
    if (easing == "easeOutBounce") { constexpr float n=7.5625f,d=2.75f; if(x<1/d)return n*x*x;if(x<2/d){const float y=x-1.5f/d;return n*y*y+.75f;}if(x<2.5f/d){const float y=x-2.25f/d;return n*y*y+.9375f;}const float y=x-2.625f/d;return n*y*y+.984375f; }
    if (easing == "easeOutElastic") return x==0||x==1?x:std::pow(2.0f,-10*x)*std::sin((x*10-.75f)*2*std::numbers::pi_v<float>/3)+1;
    return x;
}

float deterministicRandom(int index, int variant, int channel) {
    unsigned value = unsigned(index+1)*0x9e3779b9U; value ^= unsigned(variant+11)*0x85ebca6bU; value ^= unsigned(channel+23)*0xc2b2ae35U; value ^= value>>16; value*=0x7feb352dU; value^=value>>15; return float(value&0xffffU)/65535.0f;
}

RuntimeConfiguration loadConfiguration(const std::filesystem::path &dataDirectory) {
    RuntimeConfiguration result;
    const auto root = json::parse(readFile(dataDirectory / "runtime" / "state.json"));
    if (static_cast<int>(root.at("schemaVersion").number()) != 1) throw std::runtime_error("unsupported state schema");
    result.enabled = root.at("enabled").boolean(false); result.settings = parseSettings(root.at("settings"));
    result.activeEffectId = text(root.at("activeEffectId")); result.activeRevision = text(root.at("activeRevision"));
    static const std::regex safeId("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$"), safeRevision("^sha256:[a-f0-9]{64}$");
    if (!result.activeEffectId.empty() || !result.activeRevision.empty()) {
        if (!std::regex_match(result.activeEffectId, safeId) || !std::regex_match(result.activeRevision, safeRevision)) throw std::runtime_error("unsafe active revision");
        const auto runtimePath = dataDirectory / "library" / "effects" / result.activeEffectId / "revisions" / result.activeRevision.substr(7) / "runtime.json";
        result.program = parseProgram(json::parse(readFile(runtimePath)), result.activeEffectId, result.activeRevision);
    }
    return result;
}

std::vector<DrawShape> evaluateProgram(const RuntimeProgram &program, int elapsedMs, Vec2 origin) {
    struct State { Vec2 position; Vec2 scale{1,1}; float rotation=0,opacity=1; int globalStart=0; bool active=true; };
    std::vector<State> states(program.nodes.size()); std::vector<DrawShape> result; result.reserve(program.nodes.size());
    for (std::size_t index=0;index<program.nodes.size();++index) {
        const auto &node=program.nodes[index]; State state; const State *parent=node.parentIndex>=0?&states[static_cast<std::size_t>(node.parentIndex)]:nullptr;
        state.globalStart=(parent?parent->globalStart:0)+node.startMs; const float localTime=float(elapsedMs-state.globalStart); state.active=node.visible&&(!parent||parent->active)&&localTime>=0&&localTime<=node.durationMs;
        Vec2 position=node.position,scale=node.scale; float rotation=node.rotation,opacity=node.opacity;
        for(const auto &channel:node.channels){const float value=channelValue(channel,localTime);if(channel.property=="position.x")position.x+=value;else if(channel.property=="position.y")position.y+=value;else if(channel.property=="rotation")rotation+=value;else if(channel.property=="scale.x")scale.x*=value;else if(channel.property=="scale.y")scale.y*=value;else if(channel.property=="opacity")opacity*=value;}
        if(parent){const float angle=parent->rotation*std::numbers::pi_v<float>/180;const Vec2 scaled{position.x*parent->scale.x,position.y*parent->scale.y};state.position={parent->position.x+scaled.x*std::cos(angle)-scaled.y*std::sin(angle),parent->position.y+scaled.x*std::sin(angle)+scaled.y*std::cos(angle)};state.scale={parent->scale.x*scale.x,parent->scale.y*scale.y};state.rotation=parent->rotation+rotation;state.opacity=parent->opacity*opacity;}else{state.position=position;state.scale=scale;state.rotation=rotation;state.opacity=opacity;} states[index]=state;
        if(!state.active||node.group)continue;
        DrawShape shape;shape.kind=node.shape;shape.center={origin.x+state.position.x,origin.y+state.position.y};shape.size={std::abs(node.size.x*state.scale.x),std::abs(node.size.y*state.scale.y)};shape.rotation=state.rotation;shape.sides=node.sides;shape.points=node.points;shape.innerRatio=node.innerRatio;shape.fillEnabled=node.fillEnabled;shape.fill=node.fill;shape.fill.a*=state.opacity;shape.strokeEnabled=node.strokeEnabled;shape.stroke=node.stroke;shape.stroke.a*=state.opacity;shape.strokeWidth=node.strokeWidth;shape.additive=node.additive;result.push_back(shape);
    }
    return result;
}

} // namespace rc
