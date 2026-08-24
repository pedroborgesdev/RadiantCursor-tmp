#include "json.h"
#include "runtime_model.h"

#include <cassert>
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
      "settings":{"ClickEnabled":true,"Color1":"#ff0000","Color2":"#00ff00","Color3":"#0000ff","LineWidth":2,"RingLife":500,"RingSize":50,"RingCount":3,"ShowText":false,"Font":"Segoe UI","Style":"ripple","Trigger":"press","Glow":true,"TrailEnabled":false,"TrailStyle":"dots","TrailColor":"#ffffff","TrailSize":12,"TrailLife":500,"TrailDensity":50,"TrailFrequency":30,"TrailOpacity":0.7,"TrailGlow":true,"TrailOnlyPressed":false}
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
    const auto middle = rc::evaluateProgram(*configuration.program, 250, {100, 200});
    assert(middle.size() == 1);
    assert(middle[0].center.x == 135 && middle[0].center.y == 220);
    assert(middle[0].fillEnabled && middle[0].strokeEnabled);
    std::filesystem::remove_all(root);
    std::cout << "RadiantCursor native runtime model: ok\n";
}
