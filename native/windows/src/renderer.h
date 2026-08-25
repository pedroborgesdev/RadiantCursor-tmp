#pragma once

#include "../engine/effectengine.h"

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rc {

constexpr UINT RuntimeReloadMessage = WM_APP + 41;
constexpr UINT RuntimeStopMessage = WM_APP + 42;

template<typename T> class ComHandle {
public:
    ComHandle() = default;
    ~ComHandle() { reset(); }
    ComHandle(const ComHandle &) = delete;
    ComHandle &operator=(const ComHandle &) = delete;
    ComHandle(ComHandle &&other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    ComHandle &operator=(ComHandle &&other) noexcept { if (this != &other) { reset(); value_ = other.value_; other.value_ = nullptr; } return *this; }
    T *get() const { return value_; }
    T **put() { reset(); return &value_; }
    T *operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset(T *replacement = nullptr) { if (value_) value_->Release(); value_ = replacement; }
private:
    T *value_ = nullptr;
};

class RuntimeHost {
public:
    explicit RuntimeHost(std::filesystem::path dataDirectory);
    ~RuntimeHost();
    bool initialize(std::wstring &error);
    int run();
    HWND controlWindow() const { return controlWindow_; }

private:
    struct Overlay;
    struct ClickEvent;
    struct TrailParticle;

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK mouseProcedure(int code, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK monitorProcedure(HMONITOR monitor, HDC, LPRECT, LPARAM context);

    bool createGraphics(std::wstring &error);
    bool rebuildOverlays(std::wstring &error);
    bool createOverlay(HMONITOR monitor, const RECT &bounds, std::wstring &error);
    void destroyOverlays();
    bool reloadConfiguration();
    void handleMouseEvent(WPARAM message, const MSLLHOOKSTRUCT &data);
    void handleMouseMove(POINT position, std::uint64_t now);
    POINT cursorTrailOrigin(POINT hotspotPosition, Vec2 movement);
    void tick();
    void emitTrail(POINT position, POINT previous, std::uint64_t now);
    Vec2 currentCursorCenterOffset() const;
    void render(std::uint64_t now);
    void drawEvent(Overlay &overlay, const ClickEvent &event, float progress);
    void drawRipple(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawPulse(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawTarget(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawBurst(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawSpark(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawFocus(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawHalo(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawShockwave(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawOrbit(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawPetals(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawDiamond(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawSonar(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawVortex(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawCross(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawConfetti(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawLightning(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawBubbles(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawHeart(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawInk(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawSplash(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawNova(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawComet(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawEclipse(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawPlasma(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawPixelBurst(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawPrism(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawFlower(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawMeteor(Overlay &overlay, const ClickEvent &event, Color color, float progress);
    void drawTrailPoint(Overlay &overlay, const TrailParticle &point, float progress);
    void drawHalo(Overlay &overlay, std::uint64_t now);
    void drawDeclarative(Overlay &overlay, const ClickEvent &event, int elapsed);
    void drawCircle(Overlay &overlay, Vec2 center, float radius, Color color, float width, bool filled, bool glow = false);
    void drawPolygon(Overlay &overlay, const std::vector<Vec2> &points, Color color, float width, bool filled, bool closed = true);
    void drawLineSegments(Overlay &overlay, const std::vector<Vec2> &points, Color color, float width);
    void drawLineStrip(Overlay &overlay, const std::vector<Vec2> &points, Color color, float width, bool closed = false);
    void drawRenderCommand(Overlay &overlay, const RadiantCursorEngine::RenderCommand &command);
    void drawLabel(Overlay &overlay, const ClickEvent &event, float alpha);
    Vec2 local(const Overlay &overlay, Vec2 global) const;
    float displayScaleAt(POINT position) const;
    bool visibleOn(const Overlay &overlay, Vec2 position, float margin) const;

    std::filesystem::path dataDirectory_;
    RuntimeConfiguration configuration_;
    std::shared_ptr<const RadiantCursorEngine::CompiledEffect> activeProgram_;
    std::shared_ptr<const RadiantCursorEngine::CompiledEffect> haloProgram_;
    HWND controlWindow_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    std::vector<std::unique_ptr<Overlay>> overlays_;
    std::vector<ClickEvent> clicks_;
    std::vector<TrailParticle> trail_;
    std::unique_ptr<TrailParticle> liveTrailHead_;
    std::vector<RadiantCursorEngine::RenderCommand> renderCommands_;
    POINT previousCursor_{};
    POINT lastTrailEmissionCursor_{};
    bool havePreviousCursor_ = false;
    bool haveTrailEmissionCursor_ = false;
    bool anyButtonPressed_ = false;
    bool drawingTrail_ = false;
    bool drawingHalo_ = false;
    POINT cursorPosition_{};
    bool haveCursorPosition_ = false;
    bool hadVisibleFrame_ = false;
    unsigned eventSequence_ = 0;
    unsigned trailSequence_ = 0;
    std::uint64_t lastTrailEmission_ = 0;
    std::uint64_t lastFrame_ = 0;

    ComHandle<ID3D11Device> d3dDevice_;
    ComHandle<ID3D11DeviceContext> d3dContext_;
    ComHandle<IDXGIDevice> dxgiDevice_;
    ComHandle<IDXGIFactory2> dxgiFactory_;
    ComHandle<ID2D1Factory1> d2dFactory_;
    ComHandle<ID2D1Device> d2dDevice_;
    ComHandle<ID2D1DeviceContext> d2dContext_;
    ComHandle<ID2D1SolidColorBrush> brush_;
    ComHandle<IDCompositionDevice> compositionDevice_;
    ComHandle<IDWriteFactory> writeFactory_;

    static RuntimeHost *instance_;
};

} // namespace rc
