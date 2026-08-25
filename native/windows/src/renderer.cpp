#include "renderer.h"
#include "../../common/trailgeometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <sstream>

namespace rc {
namespace {
constexpr wchar_t WindowClassName[] = L"RadiantCursor.Runtime.Overlay";
constexpr UINT_PTR TickTimer = 1;
constexpr float Pi = std::numbers::pi_v<float>;

std::uint64_t clockMs() { return GetTickCount64(); }
float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
float easeOut(float value) { const float x = clamp01(value); return 1.0f - std::pow(1.0f - x, 3.0f); }
float eventAlpha(float value) { const float x = clamp01(value); return (1.0f-x)*(1.0f-x); }
float variantRotation(int variant) { constexpr float rotations[]{0.0f,.73f,1.91f,3.28f}; return rotations[std::clamp(variant,0,3)]; }
Color alpha(Color color, float amount) { color.a *= clamp01(amount); return color; }
Color lighter(Color color, float amount = 1.35f) { color.r=std::min(1.0f,color.r*amount); color.g=std::min(1.0f,color.g*amount); color.b=std::min(1.0f,color.b*amount); return color; }
Color hsv(float hue,float saturation,float value){const float h=hue*6,x=value*(1-saturation*std::abs(std::fmod(h,2.0f)-1)),m=value*(1-saturation);if(h<1)return{value,x,m,1};if(h<2)return{x,value,m,1};if(h<3)return{m,value,x,1};if(h<4)return{m,x,value,1};if(h<5)return{x,m,value,1};return{value,m,x,1};}
Vec2 add(Vec2 a, Vec2 b) { return {a.x+b.x,a.y+b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x-b.x,a.y-b.y}; }
Vec2 mul(Vec2 a, float value) { return {a.x*value,a.y*value}; }
float length(Vec2 value) { return std::hypot(value.x,value.y); }
Vec2 normalize(Vec2 value) { const float size=length(value); return size>.001f?mul(value,1.0f/size):Vec2{1,0}; }
Vec2 direction(float angle) { return {std::cos(angle),std::sin(angle)}; }

enum class ResizeDirection { None, Horizontal, Vertical, DiagonalNwSe, DiagonalNeSw };

ResizeDirection resizeDirectionForHitTest(LRESULT hitTest) {
    switch (hitTest) {
    case HTLEFT:
    case HTRIGHT:
        return ResizeDirection::Horizontal;
    case HTTOP:
    case HTBOTTOM:
        return ResizeDirection::Vertical;
    case HTTOPLEFT:
    case HTBOTTOMRIGHT:
        return ResizeDirection::DiagonalNwSe;
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
        return ResizeDirection::DiagonalNeSw;
    default:
        return ResizeDirection::None;
    }
}

struct WindowHitTestContext {
    POINT point{};
    DWORD runtimeProcessId=0;
    LRESULT result=HTNOWHERE;
};

BOOL CALLBACK findWindowHitTest(HWND window,LPARAM parameter) {
    auto &context=*reinterpret_cast<WindowHitTestContext*>(parameter);
    if(!IsWindowVisible(window)||IsIconic(window))return TRUE;
    DWORD processId=0;GetWindowThreadProcessId(window,&processId);
    if(processId==context.runtimeProcessId)return TRUE;
    const auto extendedStyle=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
    if(extendedStyle&WS_EX_TRANSPARENT)return TRUE;
    RECT bounds{};if(!GetWindowRect(window,&bounds)||!PtInRect(&bounds,context.point))return TRUE;
    DWORD_PTR hitTest=HTNOWHERE;
    const LPARAM screenPoint=MAKELPARAM(static_cast<short>(context.point.x),static_cast<short>(context.point.y));
    if(!SendMessageTimeoutW(window,WM_NCHITTEST,0,screenPoint,SMTO_ABORTIFHUNG|SMTO_BLOCK,20,&hitTest))return TRUE;
    if(static_cast<LRESULT>(hitTest)==HTTRANSPARENT||static_cast<LRESULT>(hitTest)==HTNOWHERE)return TRUE;
    context.result=static_cast<LRESULT>(hitTest);return FALSE;
}

ResizeDirection nonClientResizeDirectionAt(POINT point) {
    WindowHitTestContext context{point,GetCurrentProcessId(),HTNOWHERE};
    EnumWindows(findWindowHitTest,reinterpret_cast<LPARAM>(&context));
    return resizeDirectionForHitTest(context.result);
}

std::wstring wide(std::string_view input) {
    if (input.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0);
    if(count<=0)return {};
    std::wstring result(static_cast<std::size_t>(count),L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),result.data(),count); return result;
}

std::vector<Vec2> regular(Vec2 center,int sides,float radius,float rotation=0) {
    std::vector<Vec2> result; result.reserve(static_cast<std::size_t>(sides));
    for(int index=0;index<sides;++index){const float angle=rotation+2*Pi*index/sides;result.push_back(add(center,mul(direction(angle),radius)));} return result;
}

std::vector<Vec2> star(Vec2 center,int points,float outer,float inner,float rotation=-Pi/2) {
    std::vector<Vec2> result;result.reserve(static_cast<std::size_t>(points*2));for(int index=0;index<points*2;++index){const float angle=rotation+Pi*index/points;result.push_back(add(center,mul(direction(angle),index%2?inner:outer)));}return result;
}

bool succeeded(HRESULT value, std::wstring &error, const wchar_t *operation) {
    if (SUCCEEDED(value)) return true;
    std::wstringstream stream; stream << operation << L" falhou (0x" << std::hex << static_cast<unsigned long>(value) << L")"; error=stream.str(); return false;
}
}

struct RuntimeHost::Overlay {
    HMONITOR monitor=nullptr; RECT bounds{}; HWND window=nullptr; float scale=1.0f; bool visible=false;
    ComHandle<IDXGISwapChain1> swapChain; ComHandle<ID2D1Bitmap1> targetBitmap;
    ComHandle<IDCompositionTarget> compositionTarget; ComHandle<IDCompositionVisual> visual;
};

struct RuntimeHost::ClickEvent {
    POINT position{}; int button=0; bool pressed=true; int variant=0; std::uint64_t started=0; int lifetime=500; std::shared_ptr<const RadiantCursorEngine::CompiledEffect> program;
};

struct RuntimeHost::TrailParticle {
    Vec2 position; Vec2 direction{1,0}; float scale=1; int variant=0; unsigned serial=0; std::uint64_t started=0;
};

RuntimeHost *RuntimeHost::instance_=nullptr;

RuntimeHost::RuntimeHost(std::filesystem::path dataDirectory):dataDirectory_(std::move(dataDirectory)){}
RuntimeHost::~RuntimeHost(){if(mouseHook_)UnhookWindowsHookEx(mouseHook_);if(controlWindow_)KillTimer(controlWindow_,TickTimer);destroyOverlays();if(controlWindow_)DestroyWindow(controlWindow_);instance_=nullptr;}

bool RuntimeHost::initialize(std::wstring &error) {
    instance_=this;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW windowClass{};windowClass.cbSize=sizeof(windowClass);windowClass.lpfnWndProc=windowProcedure;windowClass.hInstance=GetModuleHandleW(nullptr);windowClass.lpszClassName=WindowClassName;windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&windowClass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){error=L"Não foi possível registrar a janela do runtime.";return false;}
    controlWindow_=CreateWindowExW(0,WindowClassName,L"RadiantCursor Runtime",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),this);
    if(!controlWindow_){error=L"Não foi possível criar o controlador do runtime.";return false;}
    if(!createGraphics(error)||!rebuildOverlays(error))return false;
    reloadConfiguration();
    mouseHook_=SetWindowsHookExW(WH_MOUSE_LL,mouseProcedure,GetModuleHandleW(nullptr),0);
    if(!mouseHook_){error=L"Não foi possível instalar o monitor global do mouse.";return false;}
    SetTimer(controlWindow_,TickTimer,15,nullptr);return true;
}

bool RuntimeHost::createGraphics(std::wstring &error) {
    UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};D3D_FEATURE_LEVEL selected{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,levels,3,D3D11_SDK_VERSION,d3dDevice_.put(),&selected,d3dContext_.put());
    if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags&~D3D11_CREATE_DEVICE_DEBUG,levels,3,D3D11_SDK_VERSION,d3dDevice_.put(),&selected,d3dContext_.put());
    if(!succeeded(hr,error,L"D3D11CreateDevice"))return false;
    if(!succeeded(d3dDevice_->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgiDevice_.put())),error,L"IDXGIDevice"))return false;
    ComHandle<IDXGIAdapter> adapter;if(!succeeded(dxgiDevice_->GetAdapter(adapter.put()),error,L"GetAdapter"))return false;
    if(!succeeded(adapter->GetParent(__uuidof(IDXGIFactory2),reinterpret_cast<void**>(dxgiFactory_.put())),error,L"IDXGIFactory2"))return false;
    D2D1_FACTORY_OPTIONS options{};
    if(!succeeded(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),&options,reinterpret_cast<void**>(d2dFactory_.put())),error,L"D2D1CreateFactory"))return false;
    if(!succeeded(d2dFactory_->CreateDevice(dxgiDevice_.get(),d2dDevice_.put()),error,L"ID2D1Device"))return false;
    if(!succeeded(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,d2dContext_.put()),error,L"ID2D1DeviceContext"))return false;
    if(!succeeded(d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),brush_.put()),error,L"CreateSolidColorBrush"))return false;
    if(!succeeded(DCompositionCreateDevice(dxgiDevice_.get(),__uuidof(IDCompositionDevice),reinterpret_cast<void**>(compositionDevice_.put())),error,L"DCompositionCreateDevice"))return false;
    if(!succeeded(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writeFactory_.put())),error,L"DWriteCreateFactory"))return false;
    return true;
}

BOOL CALLBACK RuntimeHost::monitorProcedure(HMONITOR monitor,HDC,LPRECT bounds,LPARAM context){std::wstring error;return reinterpret_cast<RuntimeHost*>(context)->createOverlay(monitor,*bounds,error)?TRUE:FALSE;}

bool RuntimeHost::rebuildOverlays(std::wstring &error){destroyOverlays();if(!EnumDisplayMonitors(nullptr,nullptr,monitorProcedure,reinterpret_cast<LPARAM>(this))){error=L"Não foi possível enumerar os monitores.";return false;}if(overlays_.empty()){error=L"Nenhum monitor ativo foi encontrado.";return false;}return true;}

bool RuntimeHost::createOverlay(HMONITOR monitor,const RECT &bounds,std::wstring &error){
    auto overlay=std::make_unique<Overlay>();overlay->monitor=monitor;overlay->bounds=bounds;const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    // WS_EX_TRANSPARENT only guarantees cross-process mouse pass-through for
    // layered windows. WM_NCHITTEST/HTTRANSPARENT below is not sufficient by
    // itself because it only continues hit-testing windows from this thread.
    constexpr DWORD extended=WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP;
    overlay->window=CreateWindowExW(extended,WindowClassName,L"RadiantCursor Overlay",WS_POPUP,bounds.left,bounds.top,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),this);
    if(!overlay->window){error=L"Não foi possível criar uma janela de overlay.";return false;}
    overlay->scale=static_cast<float>(GetDpiForWindow(overlay->window))/96.0f;
    if(!SetLayeredWindowAttributes(overlay->window,0,255,LWA_ALPHA)){error=L"Não foi possível tornar o overlay transparente a cliques.";DestroyWindow(overlay->window);return false;}
    DXGI_SWAP_CHAIN_DESC1 descriptor{};descriptor.Width=static_cast<UINT>(width);descriptor.Height=static_cast<UINT>(height);descriptor.Format=DXGI_FORMAT_B8G8R8A8_UNORM;descriptor.SampleDesc.Count=1;descriptor.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;descriptor.BufferCount=2;descriptor.Scaling=DXGI_SCALING_STRETCH;descriptor.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;descriptor.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
    if(!succeeded(dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.get(),&descriptor,nullptr,overlay->swapChain.put()),error,L"CreateSwapChainForComposition")){DestroyWindow(overlay->window);return false;}
    ComHandle<IDXGISurface> surface;if(!succeeded(overlay->swapChain->GetBuffer(0,__uuidof(IDXGISurface),reinterpret_cast<void**>(surface.put())),error,L"GetBuffer"))return false;
    const D2D1_BITMAP_PROPERTIES1 properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    if(!succeeded(d2dContext_->CreateBitmapFromDxgiSurface(surface.get(),&properties,overlay->targetBitmap.put()),error,L"CreateBitmapFromDxgiSurface"))return false;
    if(!succeeded(compositionDevice_->CreateTargetForHwnd(overlay->window,TRUE,overlay->compositionTarget.put()),error,L"CreateTargetForHwnd"))return false;
    if(!succeeded(compositionDevice_->CreateVisual(overlay->visual.put()),error,L"CreateVisual"))return false;
    if(!succeeded(overlay->visual->SetContent(overlay->swapChain.get()),error,L"SetContent")||!succeeded(overlay->compositionTarget->SetRoot(overlay->visual.get()),error,L"SetRoot"))return false;
    ShowWindow(overlay->window,SW_SHOWNOACTIVATE);SetWindowPos(overlay->window,HWND_TOPMOST,bounds.left,bounds.top,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);overlays_.push_back(std::move(overlay));compositionDevice_->Commit();return true;
}

void RuntimeHost::destroyOverlays(){for(auto &overlay:overlays_)if(overlay->window)DestroyWindow(overlay->window);overlays_.clear();}

bool RuntimeHost::reloadConfiguration(){try{configuration_=loadConfiguration(dataDirectory_);activeProgram_=configuration_.program?std::make_shared<RadiantCursorEngine::CompiledEffect>(*configuration_.program):nullptr;haloProgram_=configuration_.haloProgram?std::make_shared<RadiantCursorEngine::CompiledEffect>(*configuration_.haloProgram):nullptr;}catch(...){configuration_.enabled=false;activeProgram_.reset();haloProgram_.reset();clicks_.clear();trail_.clear();liveTrailHead_.reset();havePreviousCursor_=false;haveTrailEmissionCursor_=false;hadVisibleFrame_=true;return false;}clicks_.clear();trail_.clear();liveTrailHead_.reset();havePreviousCursor_=false;haveTrailEmissionCursor_=false;GetCursorPos(&cursorPosition_);haveCursorPosition_=true;hadVisibleFrame_=true;return true;}

LRESULT CALLBACK RuntimeHost::windowProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    RuntimeHost *host=reinterpret_cast<RuntimeHost*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){host=reinterpret_cast<RuntimeHost*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(host));}
    if(host){if(message==WM_TIMER&&wParam==TickTimer){host->tick();return 0;}if(message==RuntimeReloadMessage)return host->reloadConfiguration()?1:0;if(message==RuntimeStopMessage){PostQuitMessage(0);return 0;}if(message==WM_DISPLAYCHANGE||message==WM_POWERBROADCAST){std::wstring error;host->rebuildOverlays(error);return 0;}}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wParam,lParam);
}

LRESULT CALLBACK RuntimeHost::mouseProcedure(int code,WPARAM wParam,LPARAM lParam){if(code==HC_ACTION&&instance_){const auto &data=*reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);if(!(data.flags&LLMHF_INJECTED))instance_->handleMouseEvent(wParam,data);}return CallNextHookEx(nullptr,code,wParam,lParam);}

void RuntimeHost::handleMouseEvent(WPARAM message,const MSLLHOOKSTRUCT &data){
    const auto now=clockMs();
    anyButtonPressed_=(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_MBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000);
    if(message==WM_MOUSEMOVE){handleMouseMove(data.pt,now);return;}
    bool down=false,up=false;int button=-1;switch(message){case WM_LBUTTONDOWN:button=0;down=true;break;case WM_LBUTTONUP:button=0;up=true;break;case WM_MBUTTONDOWN:button=1;down=true;break;case WM_MBUTTONUP:button=1;up=true;break;case WM_RBUTTONDOWN:button=2;down=true;break;case WM_RBUTTONUP:button=2;up=true;break;default:return;}
    if(!configuration_.enabled||!configuration_.settings.clickEnabled)return;
    const bool accepts=(down&&(configuration_.settings.trigger=="press"||configuration_.settings.trigger=="both"))||(up&&(configuration_.settings.trigger=="release"||configuration_.settings.trigger=="both"));if(!accepts)return;
    constexpr int order[]{0,2,3,1};ClickEvent event;event.position=data.pt;event.button=button;event.pressed=down;event.variant=order[eventSequence_++%4];event.started=now;event.program=activeProgram_;event.lifetime=event.program?event.program->durationMs:configuration_.settings.lifeMs;clicks_.push_back(std::move(event));if(clicks_.size()>24)clicks_.erase(clicks_.begin());
}

void RuntimeHost::handleMouseMove(POINT position,std::uint64_t now){
    cursorPosition_=position;haveCursorPosition_=true;
    if(!havePreviousCursor_){previousCursor_=position;lastTrailEmissionCursor_=position;havePreviousCursor_=true;haveTrailEmissionCursor_=true;return;}
    const Vec2 movement{float(position.x-previousCursor_.x),float(position.y-previousCursor_.y)};
    previousCursor_=position;
    if(length(movement)<.01f)return;
    const bool trailActive=configuration_.enabled&&configuration_.settings.trailEnabled&&(!configuration_.settings.trailOnlyPressed||anyButtonPressed_);
    if(!trailActive){lastTrailEmissionCursor_=position;haveTrailEmissionCursor_=true;liveTrailHead_.reset();return;}
    const POINT start=haveTrailEmissionCursor_?lastTrailEmissionCursor_:previousCursor_;
    const Vec2 accumulatedMovement{float(position.x-start.x),float(position.y-start.y)};
    const Vec2 trailMovement=length(accumulatedMovement)>.01f?accumulatedMovement:movement;
    const Vec2 motion=normalize(trailMovement);constexpr int order[]{0,2,3,1};const int variant=order[trailSequence_%4];
    const POINT origin=cursorTrailOrigin(position,trailMovement);
    const TrailParticle head{{float(origin.x),float(origin.y)},motion,1.0f,variant,trailSequence_*64u,now};if(liveTrailHead_)*liveTrailHead_=head;else liveTrailHead_=std::make_unique<TrailParticle>(head);
    const auto interval=static_cast<std::uint64_t>(std::max(1,static_cast<int>(std::ceil(1000.0/configuration_.settings.trailFrequency))));
    if(now-lastTrailEmission_<interval)return;
    emitTrail(position,start,now);lastTrailEmission_=now;lastTrailEmissionCursor_=position;haveTrailEmissionCursor_=true;
}

POINT RuntimeHost::cursorTrailOrigin(POINT hotspotPosition,Vec2 movement){
    const float scale=displayScaleAt(hotspotPosition);
    const Vec2 offset=currentCursorCenterOffset();
    const RadiantCursorTrail::Vector center{
        float(hotspotPosition.x)+offset.x*scale,
        float(hotspotPosition.y)+offset.y*scale,
    };
    const auto origin=RadiantCursorTrail::positionBehind(center,{movement.x,movement.y},configuration_.settings.trailDistance*scale,0);
    return{LONG(std::lround(origin.x)),LONG(std::lround(origin.y))};
}

Vec2 RuntimeHost::currentCursorCenterOffset()const{
    const auto&s=configuration_.settings;const Vec2 fallback{s.trailOffsetX,s.trailOffsetY};CURSORINFO info{};info.cbSize=sizeof(info);if(!GetCursorInfo(&info)||!(info.flags&CURSOR_SHOWING)||!info.hCursor)return fallback;const HCURSOR cursor=info.hCursor;
    const auto is=[cursor](LPCWSTR id){return cursor==LoadCursorW(nullptr,id);};
    if(is(IDC_IBEAM))return s.cursorTextOffset;
    if(is(IDC_HAND))return s.cursorLinkOffset;
    if(is(IDC_CROSS))return s.cursorCrosshairOffset;
    if(is(IDC_WAIT)||is(IDC_APPSTARTING))return s.cursorBusyOffset;
    if(is(IDC_SIZEALL))return s.cursorMoveOffset;
    if(is(IDC_NO))return s.cursorForbiddenOffset;
    if(is(IDC_HELP))return s.cursorHelpOffset;
    if(is(IDC_SIZEWE))return s.cursorResizeHorizontalOffset;
    if(is(IDC_SIZENS))return s.cursorResizeVerticalOffset;
    if(is(IDC_SIZENWSE))return s.cursorResizeDiagonalNwSeOffset;
    if(is(IDC_SIZENESW))return s.cursorResizeDiagonalNeSwOffset;
    // Window frames can be owned by DWM or a custom non-client area and use a
    // cursor handle different from the shared IDC_SIZE* handles. Ask the
    // underlying top-level window which border/corner is under the pointer.
    switch(nonClientResizeDirectionAt(info.ptScreenPos)){
    case ResizeDirection::Horizontal:return s.cursorResizeHorizontalOffset;
    case ResizeDirection::Vertical:return s.cursorResizeVerticalOffset;
    case ResizeDirection::DiagonalNwSe:return s.cursorResizeDiagonalNwSeOffset;
    case ResizeDirection::DiagonalNeSw:return s.cursorResizeDiagonalNeSwOffset;
    case ResizeDirection::None:break;
    }
    return fallback;
}

void RuntimeHost::tick(){
    const auto now=clockMs();
    POINT sampled{};if(GetCursorPos(&sampled)){cursorPosition_=sampled;haveCursorPosition_=true;}
    clicks_.erase(std::remove_if(clicks_.begin(),clicks_.end(),[&](const ClickEvent&e){return now-e.started>static_cast<std::uint64_t>(e.lifetime);}),clicks_.end());trail_.erase(std::remove_if(trail_.begin(),trail_.end(),[&](const TrailParticle&p){return now-p.started>static_cast<std::uint64_t>(configuration_.settings.trailLifeMs);}),trail_.end());
    if(liveTrailHead_&&now-liveTrailHead_->started>static_cast<std::uint64_t>(configuration_.settings.trailLifeMs))liveTrailHead_.reset();
    if(now-lastFrame_>=15){render(now);lastFrame_=now;}
}

void RuntimeHost::emitTrail(POINT position,POINT previous,std::uint64_t now){
    const Vec2 emissionSpan{float(position.x-previous.x),float(position.y-previous.y)};const float distance=length(emissionSpan);if(distance<.01f)return;const Vec2 motion=normalize(emissionSpan);const float dpiScale=displayScaleAt(position);const float spacing=(2.5f+(100-configuration_.settings.trailDensity)*.42f)*dpiScale;const int samples=std::clamp(static_cast<int>(std::floor(distance/spacing)),1,6);const int particles=2+(configuration_.settings.trailDensity-1)*4/99;constexpr int order[]{0,2,3,1};const unsigned emission=trailSequence_++;const int variant=order[emission%4];const POINT trailOrigin=cursorTrailOrigin(position,emissionSpan);
    for(int sample=1;sample<=samples;++sample){const float amount=float(sample)/samples;const float distanceBehind=distance*(1.0f-amount);for(int particle=0;particle<particles;++particle){const int index=sample*8+particle;const float spread=configuration_.settings.trailSize*dpiScale*(.18f+.82f*deterministicRandom(index,variant,1));const float lateralOffset=(deterministicRandom(index,variant,0)-.5f)*spread*.9f;const auto placed=RadiantCursorTrail::positionBehind({float(trailOrigin.x),float(trailOrigin.y)},{emissionSpan.x,emissionSpan.y},distanceBehind,lateralOffset);const float directionAngle=std::atan2(motion.y,motion.x)+(deterministicRandom(index,variant,2)-.5f)*.9f;trail_.push_back({{placed.x,placed.y},direction(directionAngle),.48f+.62f*deterministicRandom(index,variant,3),variant,emission*64u+unsigned(index),now});}}
    if(trail_.size()>420)trail_.erase(trail_.begin(),trail_.begin()+static_cast<std::ptrdiff_t>(trail_.size()-420));
}

Vec2 RuntimeHost::local(const Overlay &overlay,Vec2 global)const{return{global.x-overlay.bounds.left,global.y-overlay.bounds.top};}
float RuntimeHost::displayScaleAt(POINT position)const{for(const auto&overlay:overlays_)if(position.x>=overlay->bounds.left&&position.x<overlay->bounds.right&&position.y>=overlay->bounds.top&&position.y<overlay->bounds.bottom)return overlay->scale;return 1.0f;}
bool RuntimeHost::visibleOn(const Overlay&overlay,Vec2 position,float margin)const{return position.x+margin>=overlay.bounds.left&&position.x-margin<overlay.bounds.right&&position.y+margin>=overlay.bounds.top&&position.y-margin<overlay.bounds.bottom;}

void RuntimeHost::render(std::uint64_t now){
    const bool haloActive=configuration_.settings.haloEnabled||haloProgram_;const bool active=configuration_.enabled&&(!clicks_.empty()||!trail_.empty()||liveTrailHead_||haloActive);if(!active&&!hadVisibleFrame_)return;
    bool deviceLost=false;
    for(auto &holder:overlays_){
        auto &overlay=*holder;
        const float trailMargin=configuration_.settings.trailSize*5.0f*overlay.scale;
        auto particleVisible=[&](const TrailParticle&particle){return visibleOn(overlay,particle.position,trailMargin);};
        auto clickVisible=[&](const ClickEvent&event){const float logical=event.program?event.program->maximumBounds:configuration_.settings.size*2.0f;return visibleOn(overlay,{float(event.position.x),float(event.position.y)},logical*overlay.scale);};
        const float haloScale=haveCursorPosition_?displayScaleAt(cursorPosition_):1.0f;const Vec2 haloOffset=currentCursorCenterOffset();const Vec2 haloCenter{float(cursorPosition_.x)+haloOffset.x*haloScale,float(cursorPosition_.y)+haloOffset.y*haloScale};const float haloMargin=haloProgram_?haloProgram_->maximumBounds:configuration_.settings.haloDistance+configuration_.settings.haloSize*5.0f;const bool haloVisible=haloActive&&haveCursorPosition_&&visibleOn(overlay,haloCenter,haloMargin*haloScale);const bool overlayActive=active&&(haloVisible||std::any_of(trail_.begin(),trail_.end(),particleVisible)||(liveTrailHead_&&particleVisible(*liveTrailHead_))||std::any_of(clicks_.begin(),clicks_.end(),clickVisible));
        if(!overlayActive&&!overlay.visible)continue;
        d2dContext_->SetTarget(overlay.targetBitmap.get());d2dContext_->BeginDraw();d2dContext_->Clear(D2D1::ColorF(0,0));
        if(overlayActive){
            if(haloVisible)drawHalo(overlay,now);
            drawingTrail_=true;
            auto drawParticle=[&](const TrailParticle&particle){if(!particleVisible(particle))return;const float progress=clamp01(float(now-particle.started)/configuration_.settings.trailLifeMs);const Vec2 particleCenter=local(overlay,particle.position);d2dContext_->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(overlay.scale,overlay.scale),D2D1::Point2F(particleCenter.x,particleCenter.y)));drawTrailPoint(overlay,particle,progress);d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());};
            for (const auto &particle : trail_) drawParticle(particle);
            if (liveTrailHead_) drawParticle(*liveTrailHead_);
            drawingTrail_ = false;
            for(const auto &event:clicks_){if(!clickVisible(event))continue;const int elapsed=static_cast<int>(now-event.started);const Vec2 eventCenter=local(overlay,{float(event.position.x),float(event.position.y)});d2dContext_->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(overlay.scale,overlay.scale),D2D1::Point2F(eventCenter.x,eventCenter.y)));if(event.program)drawDeclarative(overlay,event,elapsed);else drawEvent(overlay,event,clamp01(float(elapsed)/event.lifetime));if(configuration_.settings.showText)drawLabel(overlay,event,eventAlpha(float(elapsed)/event.lifetime));d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());}
        }
        const HRESULT ended=d2dContext_->EndDraw();const HRESULT presented=overlay.swapChain->Present(0,DXGI_PRESENT_DO_NOT_WAIT);deviceLost=deviceLost||ended==D2DERR_RECREATE_TARGET||presented==DXGI_ERROR_DEVICE_REMOVED||presented==DXGI_ERROR_DEVICE_RESET;overlay.visible=overlayActive;
    }
    compositionDevice_->Commit();hadVisibleFrame_=active;
    if(deviceLost){destroyOverlays();compositionDevice_.reset();brush_.reset();d2dContext_.reset();d2dDevice_.reset();d2dFactory_.reset();dxgiFactory_.reset();dxgiDevice_.reset();d3dContext_.reset();d3dDevice_.reset();std::wstring error;if(createGraphics(error))rebuildOverlays(error);}
}

void RuntimeHost::drawCircle(Overlay &overlay,Vec2 center,float radius,Color color,float width,bool filled,bool glow){
    center=local(overlay,center);const D2D1_ELLIPSE ellipse{D2D1::Point2F(center.x,center.y),radius,radius};const bool glowEnabled=glow||(!filled&&(drawingTrail_?configuration_.settings.trailGlow:configuration_.settings.glow));
    if(glowEnabled){brush_->SetColor(D2D1::ColorF(color.r,color.g,color.b,color.a*.2f));d2dContext_->DrawEllipse(ellipse,brush_.get(),std::max(2.0f,width*3.2f));}
    brush_->SetColor(D2D1::ColorF(color.r,color.g,color.b,color.a));if(filled)d2dContext_->FillEllipse(ellipse,brush_.get());else d2dContext_->DrawEllipse(ellipse,brush_.get(),std::max(1.0f,width));
}

void RuntimeHost::drawPolygon(Overlay &overlay,const std::vector<Vec2>&points,Color color,float width,bool filled,bool closed){if(points.size()<2)return;ComHandle<ID2D1PathGeometry> geometry;if(FAILED(d2dFactory_->CreatePathGeometry(geometry.put())))return;ComHandle<ID2D1GeometrySink> sink;if(FAILED(geometry->Open(sink.put())))return;const Vec2 first=local(overlay,points.front());sink->BeginFigure(D2D1::Point2F(first.x,first.y),filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);for(std::size_t i=1;i<points.size();++i){const Vec2 point=local(overlay,points[i]);sink->AddLine(D2D1::Point2F(point.x,point.y));}sink->EndFigure(closed?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);sink->Close();if(filled){brush_->SetColor(D2D1::ColorF(color.r,color.g,color.b,color.a));d2dContext_->FillGeometry(geometry.get(),brush_.get());return;}if(drawingTrail_?configuration_.settings.trailGlow:configuration_.settings.glow){brush_->SetColor(D2D1::ColorF(color.r,color.g,color.b,color.a*.2f));d2dContext_->DrawGeometry(geometry.get(),brush_.get(),std::max(2.0f,width*3.2f));}brush_->SetColor(D2D1::ColorF(color.r,color.g,color.b,color.a));d2dContext_->DrawGeometry(geometry.get(),brush_.get(),std::max(1.0f,width));}

void RuntimeHost::drawLineSegments(Overlay &overlay,const std::vector<Vec2>&points,Color color,float width){auto draw=[&](Color value,float stroke){brush_->SetColor(D2D1::ColorF(value.r,value.g,value.b,value.a));for(std::size_t index=0;index+1<points.size();index+=2){const Vec2 from=local(overlay,points[index]),to=local(overlay,points[index+1]);d2dContext_->DrawLine(D2D1::Point2F(from.x,from.y),D2D1::Point2F(to.x,to.y),brush_.get(),stroke);}};if(drawingTrail_?configuration_.settings.trailGlow:configuration_.settings.glow)draw(alpha(color,.2f),std::max(2.0f,width*3.2f));draw(color,std::max(1.0f,width));}
void RuntimeHost::drawLineStrip(Overlay &overlay,const std::vector<Vec2>&points,Color color,float width,bool closed){drawPolygon(overlay,points,color,width,false,closed);}

void RuntimeHost::drawRenderCommand(Overlay &overlay,const RadiantCursorEngine::RenderCommand&command){
    d2dContext_->SetPrimitiveBlend(command.blendMode==RadiantCursorEngine::BlendMode::Additive?D2D1_PRIMITIVE_BLEND_ADD:D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    if(command.kind==RadiantCursorEngine::RenderCommand::Kind::Circle)drawCircle(overlay,command.center,command.radius,command.color,command.width,command.filled);
    else drawPolygon(overlay,command.points,command.color,command.width,command.filled,command.kind==RadiantCursorEngine::RenderCommand::Kind::Polygon);
    d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

void RuntimeHost::drawDeclarative(Overlay &overlay,const ClickEvent&event,int elapsed){if(!event.program)return;renderCommands_.clear();event.program->evaluate(elapsed,{float(event.position.x),float(event.position.y)},configuration_.settings.colors[event.button],event.variant,renderCommands_);for(const auto&command:renderCommands_)drawRenderCommand(overlay,command);}

void RuntimeHost::drawEvent(Overlay&overlay,const ClickEvent&e,float p){const auto&s=configuration_.settings;const Color c=s.colors[e.button];if(s.style=="pulse")drawPulse(overlay,e,c,p);else if(s.style=="target")drawTarget(overlay,e,c,p);else if(s.style=="burst")drawBurst(overlay,e,c,p);else if(s.style=="spark")drawSpark(overlay,e,c,p);else if(s.style=="focus")drawFocus(overlay,e,c,p);else if(s.style=="halo")drawHalo(overlay,e,c,p);else if(s.style=="shockwave")drawShockwave(overlay,e,c,p);else if(s.style=="orbit")drawOrbit(overlay,e,c,p);else if(s.style=="petals")drawPetals(overlay,e,c,p);else if(s.style=="diamond")drawDiamond(overlay,e,c,p);else if(s.style=="sonar")drawSonar(overlay,e,c,p);else if(s.style=="vortex")drawVortex(overlay,e,c,p);else if(s.style=="cross")drawCross(overlay,e,c,p);else if(s.style=="confetti")drawConfetti(overlay,e,c,p);else if(s.style=="lightning")drawLightning(overlay,e,c,p);else if(s.style=="bubbles")drawBubbles(overlay,e,c,p);else if(s.style=="heart")drawHeart(overlay,e,c,p);else if(s.style=="ink")drawInk(overlay,e,c,p);else if(s.style=="splash")drawSplash(overlay,e,c,p);else if(s.style=="nova")drawNova(overlay,e,c,p);else if(s.style=="comet")drawComet(overlay,e,c,p);else if(s.style=="eclipse")drawEclipse(overlay,e,c,p);else if(s.style=="plasma")drawPlasma(overlay,e,c,p);else if(s.style=="pixelburst")drawPixelBurst(overlay,e,c,p);else if(s.style=="prism")drawPrism(overlay,e,c,p);else if(s.style=="flower")drawFlower(overlay,e,c,p);else if(s.style=="meteor")drawMeteor(overlay,e,c,p);else drawRipple(overlay,e,c,p);}

void RuntimeHost::drawRipple(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int rings=std::min(s.count,24);for(int i=0;i<rings;++i){const float delay=float(i)/std::max(4,rings*4);if(p<delay)continue;const float localProgress=clamp01((p-delay)/(1-delay));const float a=eventAlpha(localProgress)*(1-float(i)/float(rings+2)*.35f);drawCircle(o,center,s.size*easeOut(localProgress),alpha(c,a),s.lineWidth,false,s.glow);}}
void RuntimeHost::drawPulse(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float radius=s.size*(.18f+easeOut(p)*.82f),a=eventAlpha(p);drawCircle(o,center,radius,alpha(c,a*.17f),s.lineWidth,true);drawCircle(o,center,radius,alpha(c,a),s.lineWidth,false,s.glow);drawCircle(o,center,radius*(.28f+.35f*p),alpha(c,a*.75f),std::max(1.0f,s.lineWidth*.65f),false,s.glow);}
void RuntimeHost::drawTarget(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float r=s.size*(.42f+.58f*easeOut(p)),a=eventAlpha(p);drawCircle(o,center,r,alpha(c,a),s.lineWidth,false,s.glow);drawCircle(o,center,r*.38f,alpha(c,a*.7f),std::max(1.0f,s.lineWidth*.7f),false,s.glow);std::vector<Vec2> lines;for(int axis=0;axis<4;++axis){const Vec2 d=direction(axis*Pi/2);lines.push_back(add(center,mul(d,r*1.12f)));lines.push_back(add(center,mul(d,r*1.55f)));}drawLineSegments(o,lines,alpha(c,a*.9f),s.lineWidth);}
void RuntimeHost::drawBurst(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int rays=std::clamp(s.count*2,8,32);const float eased=easeOut(p),inner=s.size*(.08f+eased*.52f),outer=s.size*(.42f+eased*.82f);std::vector<Vec2> lines;for(int i=0;i<rays;++i){const Vec2 d=direction(2*Pi*i/rays+p*.28f);lines.push_back(add(center,mul(d,inner)));lines.push_back(add(center,mul(d,outer)));}drawLineSegments(o,lines,alpha(c,eventAlpha(p)),s.lineWidth);}
void RuntimeHost::drawSpark(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int points=std::clamp(s.count+3,5,16);const float radius=s.size*(.2f+.8f*easeOut(p)),rotation=-.2f+p*.7f,a=eventAlpha(p);std::vector<Vec2> shape;for(int i=0;i<points*2;++i)shape.push_back(add(center,mul(direction(rotation-Pi/2+Pi*i/points),i%2?radius*.34f:radius)));drawLineStrip(o,shape,alpha(c,a),s.lineWidth,true);std::vector<Vec2> sparks;for(int i=0;i<points;++i){const Vec2 d=direction(2*Pi*i/points+rotation);sparks.push_back(add(center,mul(d,radius*1.12f)));sparks.push_back(add(center,mul(d,radius*1.34f)));}drawLineSegments(o,sparks,alpha(c,a*.68f),std::max(1.0f,s.lineWidth*.7f));}
void RuntimeHost::drawFocus(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float half=s.size*(.38f+.62f*easeOut(p)),arm=half*.48f;std::vector<Vec2> lines;for(int x:{-1,1})for(int y:{-1,1}){const Vec2 corner{center.x+x*half,center.y+y*half};lines.insert(lines.end(),{corner,{corner.x-x*arm,corner.y},corner,{corner.x,corner.y-y*arm}});}drawLineSegments(o,lines,alpha(c,eventAlpha(p)),s.lineWidth);}
void RuntimeHost::drawHalo(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float eased=easeOut(p),a=eventAlpha(p),breathe=1+.08f*std::sin(p*Pi*3),r=s.size*eased*breathe;drawCircle(o,center,r,alpha(c,a*.16f),std::max(6.0f,s.lineWidth*4.5f),false);drawCircle(o,center,r,alpha(c,a*.75f),s.lineWidth,false);drawCircle(o,center,s.size*(.52f+eased*.2f),alpha(c,a*.42f),std::max(1.0f,s.lineWidth*.65f),false);}
void RuntimeHost::drawShockwave(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float r=s.size*easeOut(p),a=eventAlpha(p);drawCircle(o,center,r,alpha(c,a*.22f),std::max(5.0f,s.lineWidth*3.5f),false);drawCircle(o,center,r,alpha(c,a),s.lineWidth,false);if(p>.12f){const float echo=clamp01((p-.12f)/.88f);drawCircle(o,center,s.size*easeOut(echo)*.72f,alpha(c,eventAlpha(echo)*.62f),std::max(1.0f,s.lineWidth*.7f),false);}}
void RuntimeHost::drawOrbit(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float a=eventAlpha(p),r=s.size*(.3f+.7f*easeOut(p));drawCircle(o,center,r,alpha(c,a*.48f),std::max(1.0f,s.lineWidth*.55f),false);const int count=std::clamp(s.count,2,8);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+p*Pi*2.4f+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.22f;drawCircle(o,add(center,mul(direction(angle),r)),std::max(2.5f,s.lineWidth*1.25f),alpha(c,a),1,true);}drawCircle(o,center,std::max(2.0f,s.lineWidth),alpha(c,a*.85f),1,true);}
void RuntimeHost::drawPetals(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count+3,5,12);const float spread=s.size*easeOut(p)*.62f,r=std::max(3.0f,s.size*(.12f+p*.12f)),a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count-Pi/2+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.24f;drawCircle(o,add(center,mul(direction(angle),spread*(.86f+.14f*deterministicRandom(i,e.variant,1)))),r,alpha(c,a*.82f),s.lineWidth,false,s.glow);}drawCircle(o,center,r*1.15f,alpha(c,a*.22f),s.lineWidth,true);}
void RuntimeHost::drawDiamond(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int layers=std::clamp(s.count,1,8);for(int i=0;i<layers;++i){const float delay=float(i)/std::max(5,layers*5);if(p<delay)continue;const float localProgress=clamp01((p-delay)/(1-delay)),r=s.size*easeOut(localProgress)*(1-i*.035f);drawLineStrip(o,{{center.x,center.y-r},{center.x+r,center.y},{center.x,center.y+r},{center.x-r,center.y}},alpha(c,eventAlpha(p)*(1-i*.08f)),s.lineWidth,true);}}
void RuntimeHost::drawSonar(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float a=eventAlpha(p),heading=-.8f+p*1.6f,r=s.size*easeOut(p);drawLineSegments(o,{center,add(center,mul(direction(heading),r))},alpha(c,a),s.lineWidth);const int arcs=std::clamp(s.count,2,6);for(int arc=1;arc<=arcs;++arc){std::vector<Vec2> points;for(int i=0;i<=24;++i)points.push_back(add(center,mul(direction(heading-.78f+1.56f*i/24),r*arc/arcs)));drawLineStrip(o,points,alpha(c,a*(.4f+.6f*arc/arcs)),std::max(1.0f,s.lineWidth*.7f));}}
void RuntimeHost::drawVortex(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int arms=std::clamp(s.count,2,6);for(int arm=0;arm<arms;++arm){std::vector<Vec2> points;for(int i=0;i<=52;++i){const float t=float(i)/52;points.push_back(add(center,mul(direction(arm*2*Pi/arms+t*Pi*2.4f+p*2.2f),s.size*easeOut(p)*t)));}drawLineStrip(o,points,alpha(c,eventAlpha(p)*(1-arm*.07f)),std::max(1.0f,s.lineWidth*.72f));}}
void RuntimeHost::drawCross(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float r=s.size*easeOut(p),gap=r*.24f,a=eventAlpha(p);drawLineSegments(o,{{center.x-r,center.y},{center.x-gap,center.y},{center.x+gap,center.y},{center.x+r,center.y},{center.x,center.y-r},{center.x,center.y-gap},{center.x,center.y+gap},{center.x,center.y+r}},alpha(c,a),s.lineWidth);drawCircle(o,center,gap,alpha(c,a*.7f),std::max(1.0f,s.lineWidth*.7f),false,s.glow);}
void RuntimeHost::drawConfetti(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int pieces=std::clamp(s.count*3,10,36);std::vector<Vec2> lines;for(int i=0;i<pieces;++i){const float angle=2*Pi*i/pieces+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.62f;const Vec2 d=direction(angle),t{-d.y,d.x},point=add(center,mul(d,s.size*easeOut(p)*(.38f+.62f*deterministicRandom(i,e.variant,1))));lines.push_back(sub(point,mul(t,s.lineWidth+2)));lines.push_back(add(point,mul(t,s.lineWidth+2)));}drawLineSegments(o,lines,alpha(c,eventAlpha(p)),std::max(1.5f,s.lineWidth));}
void RuntimeHost::drawLightning(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int bolts=std::clamp(s.count,3,9);const float length=s.size*easeOut(p);for(int i=0;i<bolts;++i){const float angle=2*Pi*i/bolts-Pi/2+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.28f;const Vec2 d=direction(angle),t{-d.y,d.x};drawLineStrip(o,{add(center,mul(d,length*.12f)),add(add(center,mul(d,length*.38f)),mul(t,length*.1f)),sub(add(center,mul(d,length*.58f)),mul(t,length*.07f)),add(center,mul(d,length))},alpha(c,eventAlpha(p)),std::max(1.0f,s.lineWidth*.8f));}}
void RuntimeHost::drawBubbles(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count+2,4,14);const float travel=s.size*easeOut(p),a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count-Pi/2+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.52f,distance=travel*(.3f+.7f*deterministicRandom(i,e.variant,1)),r=std::max(2.5f,s.size*(.045f+.045f*deterministicRandom(i,e.variant,2)));const Vec2 point{center.x+std::cos(angle)*distance,center.y+std::sin(angle)*distance-p*s.size*.18f};drawCircle(o,point,r,alpha(c,a*(.55f+.1f*(i%4))),std::max(1.0f,s.lineWidth*.55f),false,s.glow);}}
void RuntimeHost::drawHeart(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const float scale=s.size*easeOut(p)/18;const Vec2 center{float(e.position.x),float(e.position.y)-s.size*.08f};std::vector<Vec2> points;for(int i=0;i<72;++i){const float t=2*Pi*i/72,x=16*std::pow(std::sin(t),3),y=13*std::cos(t)-5*std::cos(2*t)-2*std::cos(3*t)-std::cos(4*t);points.push_back({center.x+x*scale,center.y-y*scale});}drawLineStrip(o,points,alpha(c,eventAlpha(p)),s.lineWidth,true);if(s.glow)drawCircle(o,{float(e.position.x),float(e.position.y)},s.size*easeOut(p)*.9f,alpha(c,eventAlpha(p)*.08f),1,true);}
void RuntimeHost::drawInk(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float r=s.size*(.16f+.84f*easeOut(p)),a=eventAlpha(p);const int lobes=std::clamp(s.count+3,6,12);drawCircle(o,center,r*.72f,alpha(c,a*.34f),1,true);for(int i=0;i<lobes;++i){const float angle=2*Pi*i/lobes+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.4f,wobble=.68f+.3f*deterministicRandom(i,e.variant,1);drawCircle(o,add(center,mul(direction(angle),r*.42f*wobble)),r*(.28f+.05f*(i%3)),alpha(c,a*.22f),1,true);}drawCircle(o,center,std::max(2.5f,r*.12f),alpha(c,a*.68f),1,true);}
void RuntimeHost::drawSplash(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float eased=easeOut(p),a=eventAlpha(p);drawCircle(o,center,s.size*(.12f+.22f*std::sin(p*Pi)),alpha(c,a*.46f),1,true);const int drops=std::clamp(s.count*2+4,8,24);for(int i=0;i<drops;++i){const float angle=2*Pi*i/drops+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.64f,distance=s.size*eased*(.45f+.55f*deterministicRandom(i,e.variant,1)),r=std::max(2.0f,s.size*(.03f+.045f*deterministicRandom(i,e.variant,2))*(1-p*.3f));drawCircle(o,add(center,mul(direction(angle),distance)),r,alpha(c,a*(.48f+.1f*(i%3))),1,true);}}
void RuntimeHost::drawNova(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int points=std::clamp(s.count+6,8,18);const float r=s.size*easeOut(p),rotation=p*.85f-Pi/2+variantRotation(e.variant),a=eventAlpha(p);drawPolygon(o,star(center,points,r,r*(.24f+.08f*p),rotation),alpha(c,a*.34f),1,true);drawCircle(o,center,std::max(3.0f,r*.18f),alpha(c,a*.9f),1,true);drawCircle(o,center,r*.42f,alpha(c,a*.32f),1,true);}
void RuntimeHost::drawComet(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float a=eventAlpha(p),angle=-2.35f+p*1.35f+variantRotation(e.variant);const Vec2 d=direction(angle),t{-d.y,d.x},head=add(center,mul(d,s.size*easeOut(p)*.66f));for(int tail=0;tail<3;++tail){const float width=s.size*(.17f-tail*.035f),length=s.size*(.72f+tail*.18f)*easeOut(p),offset=(tail-1)*s.size*.08f;drawPolygon(o,{add(head,mul(t,width+offset)),add(sub(head,mul(d,length)),mul(t,offset)),sub(head,mul(t,width-offset))},alpha(c,a*(.16f+.08f*(2-tail))),1,true);}drawCircle(o,head,std::max(3.0f,s.size*.2f),alpha(c,a*.9f),1,true);drawCircle(o,head,s.size*.34f,alpha(c,a*.26f),1,true);}
void RuntimeHost::drawEclipse(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const float r=s.size*(.18f+.66f*easeOut(p)),a=eventAlpha(p);drawCircle(o,center,r,alpha(c,a*.2f),1,true);drawCircle(o,center,r*.48f,alpha(c,a*.82f),1,true);const float angle=p*Pi*2.2f-.8f+variantRotation(e.variant);drawCircle(o,add(center,mul(direction(angle),r*.78f)),r*.24f,alpha(lighter(c),a*.72f),1,true);drawCircle(o,center,r*1.15f,alpha(c,a*.48f),std::max(1.0f,s.lineWidth*.65f),false,s.glow);}
void RuntimeHost::drawPlasma(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count+4,7,16);const float spread=s.size*easeOut(p)*.76f,a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+p*(1.2f+.08f*i)+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.46f,orbit=spread*(.24f+.72f*deterministicRandom(i,e.variant,1)),r=std::max(2.5f,s.size*(.055f+.055f*deterministicRandom(i,e.variant,2)));drawCircle(o,{center.x+std::cos(angle)*orbit,center.y+std::sin(angle)*orbit*.72f},r,alpha(c,a*(.24f+.11f*(i%4))),1,true);}drawCircle(o,center,std::max(3.0f,s.size*.16f),alpha(c,a*.56f),1,true);}
void RuntimeHost::drawPixelBurst(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count*3,9,30);const float travel=s.size*easeOut(p),a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.76f,distance=travel*(.28f+.72f*deterministicRandom(i,e.variant,1)),half=std::max(2.0f,s.size*(.03f+.045f*deterministicRandom(i,e.variant,2)));const Vec2 point=add(center,mul(direction(angle),distance));drawPolygon(o,{{point.x-half,point.y-half},{point.x+half,point.y-half},{point.x+half,point.y+half},{point.x-half,point.y+half}},alpha(c,a*(.46f+.12f*(i%3))),1,true);}}
void RuntimeHost::drawPrism(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count*2+3,7,21);const float r=s.size*easeOut(p),a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+p*.5f+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.38f,inner=r*(.18f+.08f*(i%3)),outer=r*(.62f+.38f*deterministicRandom(i,e.variant,1)),width=s.size*(.07f+.018f*(i%3));const Vec2 d=direction(angle),t{-d.y,d.x};drawPolygon(o,{sub(add(center,mul(d,inner)),mul(t,width)),add(center,mul(d,outer)),add(add(center,mul(d,inner)),mul(t,width))},alpha(i%2?lighter(c,1.25f):c,a*(.28f+.12f*(i%3))),1,true);}}
void RuntimeHost::drawFlower(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count+4,6,14);const float a=eventAlpha(p),spread=s.size*easeOut(p)*.52f,rotation=p*1.4f+variantRotation(e.variant),r=std::max(3.0f,s.size*(.16f+.04f*std::sin(p*Pi)));for(int i=0;i<count;++i){const Vec2 point=add(center,mul(direction(2*Pi*i/count+rotation),spread));drawCircle(o,point,r*1.3f,alpha(c,a*.3f),1,true);drawCircle(o,point,r,alpha(c,a*.68f),1,true);}drawCircle(o,center,r*.82f,alpha(lighter(c,1.3f),a*.88f),1,true);}
void RuntimeHost::drawMeteor(Overlay&o,const ClickEvent&e,Color c,float p){const auto&s=configuration_.settings;const Vec2 center{float(e.position.x),float(e.position.y)};const int count=std::clamp(s.count*2+4,8,24);const float r=s.size*easeOut(p),a=eventAlpha(p);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+variantRotation(e.variant)+(deterministicRandom(i,e.variant,0)-.5f)*.42f,length=r*(.56f+.44f*deterministicRandom(i,e.variant,1)),base=r*.24f,width=s.size*(.045f+.015f*(i%3));const Vec2 d=direction(angle),t{-d.y,d.x};drawPolygon(o,{sub(add(center,mul(d,base)),mul(t,width)),add(center,mul(d,length)),add(add(center,mul(d,base)),mul(t,width))},alpha(c,a*(.2f+.12f*(i%3))),1,true);}drawCircle(o,center,r*.34f,alpha(c,a*.32f),1,true);drawCircle(o,center,std::max(3.0f,r*.13f),alpha(lighter(c,1.45f),a*.92f),1,true);}

void RuntimeHost::drawHalo(Overlay &overlay,std::uint64_t now){
    if(!haveCursorPosition_)return;
    const auto &base=configuration_.settings;constexpr int order[]{0,2,3,1};const int variant=base.haloCycleVariants?order[(now/static_cast<std::uint64_t>(std::max(1,base.haloVariantIntervalMs)))%4]:0;const float cursorScale=displayScaleAt(cursorPosition_);const Vec2 cursorOffset=currentCursorCenterOffset();const Vec2 center{float(cursorPosition_.x)+cursorOffset.x*cursorScale,float(cursorPosition_.y)+cursorOffset.y*cursorScale};
    if(haloProgram_){const int elapsed=static_cast<int>(std::fmod(double(now),double(std::max(1,haloProgram_->durationMs))));renderCommands_.clear();haloProgram_->evaluate(elapsed,center,base.haloColor,variant,renderCommands_);const Vec2 localCenter=local(overlay,center);d2dContext_->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(overlay.scale,overlay.scale),D2D1::Point2F(localCenter.x,localCenter.y)));for(const auto&command:renderCommands_)drawRenderCommand(overlay,command);d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());return;}
    Settings saved=configuration_.settings;configuration_.settings.trailStyle=base.haloStyle;configuration_.settings.trailColor=base.haloColor;configuration_.settings.trailSize=base.haloSize;configuration_.settings.trailOpacity=base.haloOpacity;configuration_.settings.trailGlow=base.haloGlow;drawingHalo_=true;drawingTrail_=true;
    const float rotationPhase=std::fmod(float(now)*.00055f*base.haloSpeed,1.0f),animationPhase=std::fmod(float(now)*.00055f,1.0f);int count=5+base.haloDensity*9/100;if(base.haloStyle=="orbitTrail")count=4;if(base.haloStyle=="ribbon"||base.haloStyle=="laser"||base.haloStyle=="neon"||base.haloStyle=="cometTrail")count=6;const float radius=base.haloDistance*cursorScale;
    for(int index=0;index<count;++index){const float angle=rotationPhase*2*Pi+variantRotation(variant)+index*2*Pi/count;const Vec2 radial=direction(angle),tangent{-radial.y,radial.x};const float radialMotion=std::min(base.haloSize*.12f*cursorScale,radius*.08f);const float ring=radius*(.92f+deterministicRandom(index,variant,0)*.16f)+std::sin(animationPhase*4*Pi+index)*radialMotion;const auto serialBase=now/static_cast<std::uint64_t>(std::max(1,base.haloVariantIntervalMs))*64;TrailParticle particle{{center.x+radial.x*ring,center.y+radial.y*ring},tangent,.48f+deterministicRandom(index,variant,2)*.44f,variant,unsigned(serialBase+index),now};const Vec2 particleCenter=local(overlay,particle.position);d2dContext_->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(overlay.scale,overlay.scale),D2D1::Point2F(particleCenter.x,particleCenter.y)));drawTrailPoint(overlay,particle,animationPhase);d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());}
    drawingTrail_=false;drawingHalo_=false;configuration_.settings=saved;
}

void RuntimeHost::drawTrailPoint(Overlay &o,const TrailParticle&p,float progress){
    const auto&s=configuration_.settings;const float fade=drawingHalo_?1.0f:(1-progress)*(1-progress),a=s.trailOpacity*fade,size=s.trailSize*p.scale*(.62f+.38f*fade);const Vec2 d=normalize(p.direction),t{-d.y,d.x};Color c=s.trailStyle=="rainbow"?hsv(std::fmod(p.serial*.047f+p.variant*.17f,1.0f),.78f,1):s.trailColor;
    if(s.trailStyle=="soft"){drawCircle(o,p.position,size*1.5f,alpha(c,a*.12f),1,true);drawCircle(o,p.position,size*.72f,alpha(c,a*.38f),1,true);}
    else if(s.trailStyle=="neon"){const std::vector<Vec2> segment{sub(p.position,mul(d,size*2.2f)),add(p.position,mul(d,size*.2f))};drawLineSegments(o,segment,alpha(c,a*.2f),std::max(4.0f,size*.75f));drawLineSegments(o,segment,alpha(lighter(c,1.5f),a*.9f),std::max(1.0f,size*.2f));}
    else if(s.trailStyle=="cometTrail"){drawPolygon(o,{add(p.position,mul(t,size*.48f)),sub(p.position,mul(d,size*3.2f)),sub(p.position,mul(t,size*.48f))},alpha(c,a*.28f),1,true);drawCircle(o,p.position,size*.58f,alpha(lighter(c,1.4f),a*.92f),1,true);}
    else if(s.trailStyle=="smoke"){const Vec2 drifted=add({p.position.x,p.position.y-progress*size*2.4f},mul(t,(p.variant-1.5f)*size*.24f));drawCircle(o,drifted,size*(1.1f+progress),alpha(c,a*.1f),1,true);drawCircle(o,drifted,size*(.58f+progress*.42f),alpha(c,a*.18f),1,true);}
    else if(s.trailStyle=="sparks"){std::vector<Vec2> rays;for(int i=0;i<4;++i){const Vec2 ray=direction(variantRotation(p.variant)+i*Pi/2);rays.push_back(add(p.position,mul(ray,size*.25f)));rays.push_back(add(p.position,mul(ray,size*(.7f+.35f*fade))));}drawLineSegments(o,rays,alpha(c,a*.85f),std::max(1.0f,size*.16f));}
    else if(s.trailStyle=="bubbleTrail"){drawCircle(o,p.position,size*.72f,alpha(c,a*.72f),std::max(1.0f,size*.13f),false);drawCircle(o,{p.position.x-size*.2f,p.position.y-size*.2f},size*.12f,alpha(lighter(c,1.6f),a*.7f),1,true);}
    else if(s.trailStyle=="stars")drawPolygon(o,star(p.position,5,size,size*.42f,variantRotation(p.variant)-Pi/2),alpha(c,a*.76f),1,true);
    else if(s.trailStyle=="hearts"){std::vector<Vec2> heart;const float scale=size/18;for(int i=0;i<30;++i){const float angle=2*Pi*i/30,x=16*std::pow(std::sin(angle),3),y=13*std::cos(angle)-5*std::cos(2*angle)-2*std::cos(3*angle)-std::cos(4*angle);heart.push_back({p.position.x+x*scale,p.position.y-y*scale});}drawPolygon(o,heart,alpha(c,a*.68f),1,true);}
    else if(s.trailStyle=="squares")drawPolygon(o,regular(p.position,4,size*.78f,variantRotation(p.variant)),alpha(c,a*.72f),1,true);
    else if(s.trailStyle=="diamonds")drawPolygon(o,regular(p.position,4,size,Pi/4),alpha(c,a*.78f),1,true);
    else if(s.trailStyle=="triangles")drawPolygon(o,regular(p.position,3,size,std::atan2(d.y,d.x)),alpha(c,a*.78f),1,true);
    else if(s.trailStyle=="ribbon")drawLineSegments(o,{add(sub(p.position,mul(d,size*2)),mul(t,size*.3f)),sub(add(p.position,mul(d,size*.35f)),mul(t,size*.3f))},alpha(c,a*.68f),std::max(2.0f,size*.52f));
    else if(s.trailStyle=="laser"){const std::vector<Vec2> beam{sub(p.position,mul(d,size*2.8f)),add(p.position,mul(d,size*.25f))};drawLineSegments(o,beam,alpha(c,a*.22f),std::max(4.0f,size*.62f));drawLineSegments(o,beam,{1,1,1,a*.9f},std::max(1.0f,size*.13f));}
    else if(s.trailStyle=="fire"){const Vec2 tip=sub(p.position,mul(d,size*(1.6f+progress)));drawPolygon(o,{add(p.position,mul(t,size*.58f)),tip,sub(p.position,mul(t,size*.58f))},alpha(c,a*.48f),1,true);drawCircle(o,p.position,size*.42f,alpha(lighter(c,1.65f),a*.84f),1,true);}
    else if(s.trailStyle=="ice"){std::vector<Vec2> crystal;for(int i=0;i<6;++i){const Vec2 arm=direction(i*Pi/3+variantRotation(p.variant));crystal.push_back(sub(p.position,mul(arm,size*.78f)));crystal.push_back(add(p.position,mul(arm,size*.78f)));}drawLineSegments(o,crystal,alpha(lighter(c,1.45f),a*.82f),std::max(1.0f,size*.12f));}
    else if(s.trailStyle=="petalTrail"){for(int i=0;i<3;++i)drawCircle(o,add(p.position,mul(direction(variantRotation(p.variant)+i*2*Pi/3),size*.38f)),size*.52f,alpha(c,a*.48f),1,true);}
    else if(s.trailStyle=="pixels"){for(int i=0;i<3;++i){const float offset=(i-1)*size*.72f,half=size*(.34f-i*.05f);const Vec2 center=add(sub(p.position,mul(d,i*size*.46f)),mul(t,offset));drawPolygon(o,{{center.x-half,center.y-half},{center.x+half,center.y-half},{center.x+half,center.y+half},{center.x-half,center.y+half}},alpha(c,a*(.72f-i*.16f)),1,true);}}
    else if(s.trailStyle=="orbitTrail"){drawCircle(o,p.position,size*.82f,alpha(c,a*.3f),std::max(1.0f,size*.08f),false);for(int i=0;i<2;++i)drawCircle(o,add(p.position,mul(direction(progress*8+variantRotation(p.variant)+i*Pi),size*.82f)),size*.2f,alpha(c,a*.82f),1,true);}
    else if(s.trailStyle=="rainbow"){drawCircle(o,p.position,size*.62f,alpha(c,a*.72f),1,true);drawCircle(o,p.position,size,alpha(lighter(c,1.45f),a*.25f),1,true);}
    else drawCircle(o,p.position,size*.54f,alpha(c,a*.82f),1,true);
}

void RuntimeHost::drawLabel(Overlay &overlay,const ClickEvent&event,float opacity){
    const wchar_t*names[]{L"Esquerdo",L"Meio",L"Direito"};std::wstring label=names[event.button];label+=event.pressed?L" \u2193":L" \u2191";std::wstring family=wide(configuration_.settings.font.substr(0,configuration_.settings.font.find(',')));if(family.empty())family=L"Segoe UI";ComHandle<IDWriteTextFormat> format;if(FAILED(writeFactory_->CreateTextFormat(family.c_str(),nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13,L"pt-BR",format.put())))return;brush_->SetColor(D2D1::ColorF(1,1,1,opacity));const Vec2 point=local(overlay,{float(event.position.x)+configuration_.settings.size+8,float(event.position.y)-9});const D2D1_RECT_F bounds{point.x,point.y,point.x+180,point.y+32};d2dContext_->DrawTextW(label.c_str(),static_cast<UINT32>(label.size()),format.get(),bounds,brush_.get());
}

int RuntimeHost::run(){MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);}

} // namespace rc
