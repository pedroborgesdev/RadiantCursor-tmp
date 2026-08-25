#include "json.h"
#include "effectengine.h"
#include "../../common/trailgeometry.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void write(const std::filesystem::path &path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << value;
}
}

int main() {
    const RadiantCursorTrail::Vector cursorCenter{108,208};
    const auto cursorOriginRight = RadiantCursorTrail::positionBehind(cursorCenter,{10,0},8,0);
    assert(cursorOriginRight.x == 100 && cursorOriginRight.y == 208);
    const auto cursorOriginLeft = RadiantCursorTrail::positionBehind(cursorCenter,{-10,0},8,0);
    assert(cursorOriginLeft.x == 116 && cursorOriginLeft.y == 208);
    const auto cursorOriginUp = RadiantCursorTrail::positionBehind(cursorCenter,{0,-10},8,0);
    assert(cursorOriginUp.x == 108 && cursorOriginUp.y == 216);
    const auto behindUp = RadiantCursorTrail::positionBehind({50,50},{0,-10},20,0);
    assert(behindUp.x == 50 && behindUp.y == 70);
    const auto behindRight = RadiantCursorTrail::positionBehind({50,50},{10,0},20,0);
    assert(behindRight.x == 30 && behindRight.y == 50);
    const auto behindDiagonal = RadiantCursorTrail::positionBehind({50,50},{10,-10},20,0);
    const float diagonalComponent = 20.0f / std::sqrt(2.0f);
    assert(std::abs(behindDiagonal.x - (50.0f - diagonalComponent)) < .001f);
    assert(std::abs(behindDiagonal.y - (50.0f + diagonalComponent)) < .001f);
    const auto diagonalWithLateral = RadiantCursorTrail::positionBehind({50,50},{3,4},10,5);
    assert(std::abs(diagonalWithLateral.x - 40.0f) < .001f);
    assert(std::abs(diagonalWithLateral.y - 45.0f) < .001f);

    const auto parsed = rc::json::parse(R"({"text":"ok","items":[1,true,null]})");
    assert(parsed.at("text").string() == "ok");
    assert(parsed.at("items").size() == 3);
    bool rejected = false;
    try { (void)rc::json::parse("{broken}"); } catch (const rc::json::Error &) { rejected = true; }
    assert(rejected);

    const auto root = std::filesystem::temp_directory_path() / "radiantcursor-native-core-test";
    std::filesystem::remove_all(root);
    const std::string revision(64, 'a');
    write(root / "runtime" / "state.json", R"({
      "schemaVersion":1,"enabled":true,"activeEffectId":"fixture","activeRevision":"sha256:)" + revision + R"(",
      "settings":{"ClickEnabled":true,"Color1":"#ff0000","Color2":"#00ff00","Color3":"#0000ff","LineWidth":2,"RingLife":500,"RingSize":50,"RingCount":3,"ShowText":false,"Font":"Segoe UI","Style":"ripple","Trigger":"press","Glow":true,"TrailEnabled":false,"TrailStyle":"dots","TrailColor":"#ffffff","TrailSize":12,"TrailLife":500,"TrailDensity":50,"TrailFrequency":30,"TrailOpacity":0.7,"TrailOffsetX":-6,"TrailOffsetY":11,"CursorTextOffsetX":-3,"CursorTextOffsetY":19,"CursorResizeDiagonalNwSeOffsetX":22,"CursorResizeDiagonalNwSeOffsetY":-4,"TrailDistance":9,"TrailGlow":true,"TrailOnlyPressed":false,"HaloEnabled":true,"HaloStyle":"stars","HaloColor":"#8bd97b","HaloSize":18,"HaloDistance":72,"HaloDensity":55,"HaloOpacity":0.82,"HaloSpeed":0,"HaloVariantInterval":10,"HaloCycleVariants":true,"HaloGlow":true}
    })");
    write(root / "library" / "effects" / "fixture" / "revisions" / revision / "runtime.json", R"({
      "runtimeVersion":1,"compilerVersion":1,"effectId":"fixture","revision":"sha256:)" + revision + R"(","durationMs":500,"maxRadius":100,
      "nodes":[{"id":"shape","name":"Shape","kind":"shape","parentIndex":-1,"subtreeEnd":1,"visible":true,"startMs":0,"durationMs":500,
      "transform":{"position":[10,20],"size":[40,60],"scale":[1,1],"rotationDeg":0,"anchor":[0.5,0.5],"skewXDeg":0},"opacity":1,
      "channels":[{"property":"position.x","composition":"add","keyframes":[{"time":0,"value":0,"easing":"linear"},{"time":500,"value":50,"easing":"linear"}]}],
      "shape":{"kind":"circle","sides":5,"points":5,"innerRatio":0.45},"appearance":{"fill":{"enabled":true,"color":"#112233"},"stroke":{"enabled":true,"color":"#ffffff","width":2},"opacity":1,"blendMode":"normal"}}]
    })");

    const auto configuration = rc::loadConfiguration(root);
    assert(configuration.enabled && configuration.program.has_value());
    assert(configuration.settings.trailOffsetX == -6 && configuration.settings.trailOffsetY == 11);
    assert(configuration.settings.cursorTextOffset.x == -3 && configuration.settings.cursorTextOffset.y == 19);
    assert(configuration.settings.cursorResizeDiagonalNwSeOffset.x == 22 && configuration.settings.cursorResizeDiagonalNwSeOffset.y == -4);
    assert(configuration.settings.trailDistance == 9);
    assert(configuration.settings.haloEnabled && configuration.settings.haloStyle == "stars");
    assert(configuration.settings.haloDistance == 72);
    assert(configuration.settings.haloSpeed == 0);
    assert(configuration.settings.haloVariantIntervalMs == 10);
    std::vector<rc::RadiantCursorEngine::RenderCommand> middle;
    configuration.program->evaluate(250, {100, 200}, configuration.settings.colors[0], 0, middle);
    assert(middle.size() == 2);
    assert(middle[0].center.x == 135 && middle[0].center.y == 220);
    assert(middle[0].kind == rc::RadiantCursorEngine::RenderCommand::Kind::Circle);
    assert(middle[0].filled && !middle[1].filled);
    // KWin defines a circle radius from the smallest source dimension.
    assert(middle[0].radius == 20.0f && middle[1].radius == 20.0f);

    using namespace rc::RadiantCursorEngine;
    CompiledEffect geometryFixture;
    CompiledLayer rectangle;
    rectangle.geometry.kind = GeometryKind::Rect;
    rectangle.geometry.size = {40,20};
    rectangle.scale = {2,.5f};
    rectangle.rotationDegrees = 90;
    rectangle.material.hasFill = true;
    rectangle.material.fillColor = {1,1,1,1};
    rectangle.material.hasStroke = false;
    geometryFixture.layers.push_back(rectangle);
    std::vector<RenderCommand> rectangleCommands;
    geometryFixture.evaluate(0,{0,0},{1,1,1,1},0,rectangleCommands);
    assert(rectangleCommands.size() == 1 && rectangleCommands[0].points.size() == 4);
    // KWin rotates local geometry first and applies the non-uniform scale after it.
    assert(std::abs(rectangleCommands[0].points[0].x-20.0f)<.001f);
    assert(std::abs(rectangleCommands[0].points[0].y+10.0f)<.001f);

    CompiledEffect circleFixture;
    CompiledLayer circle;
    circle.geometry.kind = GeometryKind::Circle;
    circle.geometry.radius = 20;
    circle.scale = {2,.5f};
    circle.material.hasFill = true;
    circle.material.fillColor = {1,1,1,1};
    circle.material.hasStroke = false;
    circleFixture.layers.push_back(circle);
    std::vector<RenderCommand> circleCommands;
    circleFixture.evaluate(0,{0,0},{1,1,1,1},0,circleCommands);
    assert(circleCommands.size() == 1 && circleCommands[0].radius == 40.0f);
    std::filesystem::remove_all(root);
    std::cout << "RadiantCursor Windows effect engine: ok\n";
}
