#include "renderer.h"

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
Vec2 add(Vec2 a, Vec2 b) { return {a.x+b.x,a.y+b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x-b.x,a.y-b.y}; }
Vec2 mul(Vec2 a, float value) { return {a.x*value,a.y*value}; }
float length(Vec2 value) { return std::hypot(value.x,value.y); }
Vec2 normalize(Vec2 value) { const float size=length(value); return size>.001f?mul(value,1.0f/size):Vec2{1,0}; }
Vec2 direction(float angle) { return {std::cos(angle),std::sin(angle)}; }

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
    HMONITOR monitor=nullptr; RECT bounds{}; HWND window=nullptr;
    ComHandle<IDXGISwapChain1> swapChain; ComHandle<ID2D1Bitmap1> targetBitmap;
    ComHandle<IDCompositionTarget> compositionTarget; ComHandle<IDCompositionVisual> visual;
};

struct RuntimeHost::ClickEvent {
    POINT position{}; int button=0; bool pressed=true; int variant=0; std::uint64_t started=0; int lifetime=500; std::shared_ptr<const RuntimeProgram> program;
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
    SetTimer(controlWindow_,TickTimer,8,nullptr);return true;
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
    if(!succeeded(DCompositionCreateDevice(dxgiDevice_.get(),__uuidof(IDCompositionDevice),reinterpret_cast<void**>(compositionDevice_.put())),error,L"DCompositionCreateDevice"))return false;
    if(!succeeded(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writeFactory_.put())),error,L"DWriteCreateFactory"))return false;
    return true;
}

BOOL CALLBACK RuntimeHost::monitorProcedure(HMONITOR monitor,HDC,LPRECT bounds,LPARAM context){std::wstring error;return reinterpret_cast<RuntimeHost*>(context)->createOverlay(monitor,*bounds,error)?TRUE:FALSE;}

bool RuntimeHost::rebuildOverlays(std::wstring &error){destroyOverlays();if(!EnumDisplayMonitors(nullptr,nullptr,monitorProcedure,reinterpret_cast<LPARAM>(this))){error=L"Não foi possível enumerar os monitores.";return false;}if(overlays_.empty()){error=L"Nenhum monitor ativo foi encontrado.";return false;}return true;}

bool RuntimeHost::createOverlay(HMONITOR monitor,const RECT &bounds,std::wstring &error){
    auto overlay=std::make_unique<Overlay>();overlay->monitor=monitor;overlay->bounds=bounds;const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    constexpr DWORD extended=WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP;
    overlay->window=CreateWindowExW(extended,WindowClassName,L"RadiantCursor Overlay",WS_POPUP,bounds.left,bounds.top,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),this);
    if(!overlay->window){error=L"Não foi possível criar uma janela de overlay.";return false;}
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

bool RuntimeHost::reloadConfiguration(){try{configuration_=loadConfiguration(dataDirectory_);activeProgram_=configuration_.program?std::make_shared<RuntimeProgram>(*configuration_.program):nullptr;}catch(...){configuration_.enabled=false;activeProgram_.reset();clicks_.clear();trail_.clear();hadVisibleFrame_=true;return false;}clicks_.clear();trail_.clear();hadVisibleFrame_=true;return true;}

LRESULT CALLBACK RuntimeHost::windowProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    RuntimeHost *host=reinterpret_cast<RuntimeHost*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){host=reinterpret_cast<RuntimeHost*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(host));}
    if(host){if(message==WM_TIMER&&wParam==TickTimer){host->tick();return 0;}if(message==RuntimeReloadMessage)return host->reloadConfiguration()?1:0;if(message==RuntimeStopMessage){PostQuitMessage(0);return 0;}if(message==WM_DISPLAYCHANGE||message==WM_POWERBROADCAST){std::wstring error;host->rebuildOverlays(error);return 0;}}
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND)return 1;
    return DefWindowProcW(window,message,wParam,lParam);
}

LRESULT CALLBACK RuntimeHost::mouseProcedure(int code,WPARAM wParam,LPARAM lParam){if(code==HC_ACTION&&instance_){const auto &data=*reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);if(!(data.flags&LLMHF_INJECTED))instance_->handleClick(wParam,data);}return CallNextHookEx(nullptr,code,wParam,lParam);}

void RuntimeHost::handleClick(WPARAM message,const MSLLHOOKSTRUCT &data){
    bool down=false,up=false;int button=-1;switch(message){case WM_LBUTTONDOWN:button=0;down=true;break;case WM_LBUTTONUP:button=0;up=true;break;case WM_MBUTTONDOWN:button=1;down=true;break;case WM_MBUTTONUP:button=1;up=true;break;case WM_RBUTTONDOWN:button=2;down=true;break;case WM_RBUTTONUP:button=2;up=true;break;default:return;}
    anyButtonPressed_=(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_MBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000);
    if(!configuration_.enabled||!configuration_.settings.clickEnabled)return;
    const bool accepts=(down&&(configuration_.settings.trigger=="press"||configuration_.settings.trigger=="both"))||(up&&(configuration_.settings.trigger=="release"||configuration_.settings.trigger=="both"));if(!accepts)return;
    constexpr int order[]{0,2,3,1};ClickEvent event;event.position=data.pt;event.button=button;event.pressed=down;event.variant=order[eventSequence_++%4];event.started=clockMs();event.program=activeProgram_;event.lifetime=event.program?event.program->durationMs:configuration_.settings.lifeMs;clicks_.push_back(std::move(event));if(clicks_.size()>24)clicks_.erase(clicks_.begin());
}

void RuntimeHost::tick(){
    const auto now=clockMs();POINT cursor{};if(GetCursorPos(&cursor)){if(havePreviousCursor_&&configuration_.enabled&&configuration_.settings.trailEnabled&&(cursor.x!=previousCursor_.x||cursor.y!=previousCursor_.y)){const auto interval=static_cast<std::uint64_t>(std::max(1,static_cast<int>(std::ceil(1000.0/configuration_.settings.trailFrequency))));if(now-lastTrailEmission_>=interval&&(!configuration_.settings.trailOnlyPressed||anyButtonPressed_)){emitTrail(cursor,previousCursor_,now);lastTrailEmission_=now;}}previousCursor_=cursor;havePreviousCursor_=true;}
    clicks_.erase(std::remove_if(clicks_.begin(),clicks_.end(),[&](const ClickEvent&e){return now-e.started>static_cast<std::uint64_t>(e.lifetime);}),clicks_.end());trail_.erase(std::remove_if(trail_.begin(),trail_.end(),[&](const TrailParticle&p){return now-p.started>static_cast<std::uint64_t>(configuration_.settings.trailLifeMs);}),trail_.end());
    if(now-lastFrame_>=15){render(now);lastFrame_=now;}
}

void RuntimeHost::emitTrail(POINT position,POINT previous,std::uint64_t now){
    const Vec2 movement{float(position.x-previous.x),float(position.y-previous.y)};const float distance=length(movement);if(distance<.01f)return;const Vec2 motion=normalize(movement);const float spacing=2.5f+(100-configuration_.settings.trailDensity)*.42f;const int samples=std::clamp(static_cast<int>(std::floor(distance/spacing)),1,6);const int particles=2+(configuration_.settings.trailDensity-1)*4/99;constexpr int order[]{0,2,3,1};const unsigned emission=trailSequence_++;const int variant=order[emission%4];
    for(int sample=1;sample<=samples;++sample){const float amount=float(sample)/samples;const Vec2 base{previous.x+movement.x*amount,previous.y+movement.y*amount};for(int particle=0;particle<particles;++particle){const int index=sample*8+particle;const float angle=variantRotation(variant)+2*Pi*particle/particles+(deterministicRandom(index,variant,0)-.5f)*.72f;const float spread=configuration_.settings.trailSize*(.18f+.82f*deterministicRandom(index,variant,1));const float directionAngle=std::atan2(motion.y,motion.x)+(deterministicRandom(index,variant,2)-.5f)*.9f;trail_.push_back({add(base,mul(direction(angle),spread)),direction(directionAngle),.48f+.62f*deterministicRandom(index,variant,3),variant,emission*64u+unsigned(index),now});}}
    if(trail_.size()>420)trail_.erase(trail_.begin(),trail_.begin()+static_cast<std::ptrdiff_t>(trail_.size()-420));
}

Vec2 RuntimeHost::local(const Overlay &overlay,Vec2 global)const{return{global.x-overlay.bounds.left,global.y-overlay.bounds.top};}

void RuntimeHost::render(std::uint64_t now){
    const bool active=configuration_.enabled&&(!clicks_.empty()||!trail_.empty());if(!active&&!hadVisibleFrame_)return;
    bool deviceLost=false;
    for(auto &holder:overlays_){auto &overlay=*holder;d2dContext_->SetTarget(overlay.targetBitmap.get());d2dContext_->BeginDraw();d2dContext_->Clear(D2D1::ColorF(0,0));
        if(active){for(const auto &particle:trail_){const float progress=clamp01(float(now-particle.started)/configuration_.settings.trailLifeMs);drawTrail(overlay,particle,progress);}for(const auto &event:clicks_){const int elapsed=static_cast<int>(now-event.started);if(event.program)drawDeclarative(overlay,event,elapsed);else drawLegacy(overlay,event,clamp01(float(elapsed)/event.lifetime));if(configuration_.settings.showText)drawLabel(overlay,event,eventAlpha(float(elapsed)/event.lifetime));}}
        const HRESULT ended=d2dContext_->EndDraw();const HRESULT presented=overlay.swapChain->Present(0,DXGI_PRESENT_DO_NOT_WAIT);deviceLost=deviceLost||ended==D2DERR_RECREATE_TARGET||presented==DXGI_ERROR_DEVICE_REMOVED||presented==DXGI_ERROR_DEVICE_RESET;
    }compositionDevice_->Commit();hadVisibleFrame_=active;
    if(deviceLost){destroyOverlays();compositionDevice_.reset();d2dContext_.reset();d2dDevice_.reset();d2dFactory_.reset();dxgiFactory_.reset();dxgiDevice_.reset();d3dContext_.reset();d3dDevice_.reset();std::wstring error;if(createGraphics(error))rebuildOverlays(error);}
}

void RuntimeHost::drawCircle(Overlay &overlay,Vec2 center,float radius,Color color,float width,bool filled,bool glow){
    center=local(overlay,center);ComHandle<ID2D1SolidColorBrush> brush;if(FAILED(d2dContext_->CreateSolidColorBrush(D2D1::ColorF(color.r,color.g,color.b,color.a),brush.put())))return;const D2D1_ELLIPSE ellipse{D2D1::Point2F(center.x,center.y),radius,radius};
    if(glow){ComHandle<ID2D1SolidColorBrush> glowBrush;d2dContext_->CreateSolidColorBrush(D2D1::ColorF(color.r,color.g,color.b,color.a*.13f),glowBrush.put());d2dContext_->DrawEllipse(ellipse,glowBrush.get(),std::max(width*4.0f,6.0f));}
    if(filled)d2dContext_->FillEllipse(ellipse,brush.get());else d2dContext_->DrawEllipse(ellipse,brush.get(),std::max(.5f,width));
}

void RuntimeHost::drawEllipse(Overlay &overlay,Vec2 center,Vec2 radius,Color color,float width,bool filled){center=local(overlay,center);ComHandle<ID2D1SolidColorBrush> brush;if(FAILED(d2dContext_->CreateSolidColorBrush(D2D1::ColorF(color.r,color.g,color.b,color.a),brush.put())))return;const D2D1_ELLIPSE ellipse{D2D1::Point2F(center.x,center.y),radius.x,radius.y};if(filled)d2dContext_->FillEllipse(ellipse,brush.get());else d2dContext_->DrawEllipse(ellipse,brush.get(),width);}

void RuntimeHost::drawPolygon(Overlay &overlay,const std::vector<Vec2>&points,Color color,float width,bool filled,bool closed){if(points.size()<2)return;ComHandle<ID2D1PathGeometry> geometry;if(FAILED(d2dFactory_->CreatePathGeometry(geometry.put())))return;ComHandle<ID2D1GeometrySink> sink;if(FAILED(geometry->Open(sink.put())))return;const Vec2 first=local(overlay,points.front());sink->BeginFigure(D2D1::Point2F(first.x,first.y),filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);for(std::size_t i=1;i<points.size();++i){const Vec2 point=local(overlay,points[i]);sink->AddLine(D2D1::Point2F(point.x,point.y));}sink->EndFigure(closed?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);sink->Close();ComHandle<ID2D1SolidColorBrush> brush;if(FAILED(d2dContext_->CreateSolidColorBrush(D2D1::ColorF(color.r,color.g,color.b,color.a),brush.put())))return;if(filled)d2dContext_->FillGeometry(geometry.get(),brush.get());else d2dContext_->DrawGeometry(geometry.get(),brush.get(),std::max(.5f,width));}

void RuntimeHost::drawShape(Overlay &overlay,const DrawShape&shape){
    d2dContext_->SetPrimitiveBlend(shape.additive?D2D1_PRIMITIVE_BLEND_ADD:D2D1_PRIMITIVE_BLEND_SOURCE_OVER);const float rotation=shape.rotation*Pi/180;std::vector<Vec2> points;
    if(shape.kind==ShapeKind::Circle&&std::abs(shape.size.x-shape.size.y)<.01f){if(shape.fillEnabled)drawCircle(overlay,shape.center,shape.size.x*.5f,shape.fill,1,true);if(shape.strokeEnabled)drawCircle(overlay,shape.center,shape.size.x*.5f,shape.stroke,shape.strokeWidth,false);d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);return;}
    const int count=shape.kind==ShapeKind::Star?shape.points*2:shape.kind==ShapeKind::Diamond?4:shape.kind==ShapeKind::Polygon?shape.sides:shape.kind==ShapeKind::Rectangle?4:shape.kind==ShapeKind::Line?2:48;points.reserve(count);
    if(shape.kind==ShapeKind::Rectangle){for(Vec2 p:std::vector<Vec2>{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}){const Vec2 scaled{p.x*shape.size.x,p.y*shape.size.y};points.push_back(add(shape.center,{scaled.x*std::cos(rotation)-scaled.y*std::sin(rotation),scaled.x*std::sin(rotation)+scaled.y*std::cos(rotation)}));}}
    else if(shape.kind==ShapeKind::Line){const Vec2 d=direction(rotation);points={add(shape.center,mul(d,-shape.size.x*.5f)),add(shape.center,mul(d,shape.size.x*.5f))};}
    else{for(int i=0;i<count;++i){const float angle=rotation-Pi/2+2*Pi*i/count;const float inner=shape.kind==ShapeKind::Star&&i%2?shape.innerRatio:1;points.push_back({shape.center.x+std::cos(angle)*shape.size.x*.5f*inner,shape.center.y+std::sin(angle)*shape.size.y*.5f*inner});}}
    if(shape.fillEnabled&&shape.kind!=ShapeKind::Line)drawPolygon(overlay,points,shape.fill,1,true);
    if(shape.strokeEnabled)drawPolygon(overlay,points,shape.stroke,shape.strokeWidth,false,shape.kind!=ShapeKind::Line);
    d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

void RuntimeHost::drawDeclarative(Overlay &overlay,const ClickEvent&event,int elapsed){if(!event.program)return;for(auto shape:evaluateProgram(*event.program,elapsed,{float(event.position.x),float(event.position.y)}))drawShape(overlay,shape);}

void RuntimeHost::drawLegacy(Overlay &overlay,const ClickEvent&event,float progress){
    const auto&s=configuration_.settings;const Vec2 center{float(event.position.x),float(event.position.y)};const Color color=s.colors[event.button];const float a=eventAlpha(progress),radius=s.size*easeOut(progress),rot=variantRotation(event.variant);auto circle=[&](Vec2 c,float r,float opacity=1.0f,bool fill=false,float width=-1){drawCircle(overlay,c,r,alpha(color,a*opacity),width<0?s.lineWidth:width,fill,s.glow&&!fill);};auto lines=[&](std::vector<Vec2> p,float opacity=1,float width=-1,bool closed=false){drawPolygon(overlay,p,alpha(color,a*opacity),width<0?s.lineWidth:width,false,closed);};
    if(s.style=="ripple"){for(int i=0;i<std::min(s.count,9);++i){const float delay=float(i)/std::max(5,s.count*5);if(progress>=delay)circle(center,s.size*easeOut((progress-delay)/(1-delay)),1.0f-i*.07f);}}
    else if(s.style=="pulse"){circle(center,radius,.18f,true);circle(center,radius*.72f,.9f);circle(center,std::max(2.0f,radius*.12f),.9f,true);}
    else if(s.style=="target"){circle(center,radius);const float gap=radius*.3f;lines({{center.x-radius,center.y},{center.x-gap,center.y}});lines({{center.x+gap,center.y},{center.x+radius,center.y}});lines({{center.x,center.y-radius},{center.x,center.y-gap}});lines({{center.x,center.y+gap},{center.x,center.y+radius}});}
    else if(s.style=="burst"){const int rays=std::clamp(s.count*2+4,8,28);for(int i=0;i<rays;++i){const float angle=2*Pi*i/rays+rot;const Vec2 d=direction(angle);lines({add(center,mul(d,radius*.28f)),add(center,mul(d,radius*(.72f+.28f*deterministicRandom(i,event.variant,1))))},.85f);}}
    else if(s.style=="spark"){const int sparks=std::clamp(s.count*3,9,30);for(int i=0;i<sparks;++i){const float angle=2*Pi*i/sparks+rot+(deterministicRandom(i,event.variant,0)-.5f)*.5f;const Vec2 d=direction(angle),t{-d.y,d.x};const Vec2 point=add(center,mul(d,radius*(.35f+.65f*deterministicRandom(i,event.variant,1))));const float half=2+s.lineWidth+3*deterministicRandom(i,event.variant,2);lines({sub(point,mul(t,half)),add(point,mul(t,half))},.8f);}}
    else if(s.style=="focus"){const float gap=radius*.45f,arm=radius*.42f;for(int x:{-1,1})for(int y:{-1,1})lines({{center.x+x*gap,center.y+y*(gap-arm)},{center.x+x*gap,center.y+y*gap},{center.x+x*(gap-arm),center.y+y*gap}});}
    else if(s.style=="halo"){circle(center,radius,.24f,true);circle(center,radius*.72f,.9f);circle(center,radius*1.08f,.45f);}
    else if(s.style=="shockwave"){circle(center,radius,.14f,true);circle(center,radius,.95f,false,std::max(2.0f,s.lineWidth*(1.8f-progress)));}
    else if(s.style=="orbit"){drawEllipse(overlay,center,{radius,radius*.42f},alpha(color,a),s.lineWidth,false);for(int i=0;i<std::clamp(s.count,2,8);++i){const float angle=2*Pi*i/std::clamp(s.count,2,8)+progress*4+rot;circle({center.x+std::cos(angle)*radius,center.y+std::sin(angle)*radius*.42f},std::max(2.0f,s.size*.06f),.9f,true);}}
    else if(s.style=="petals"||s.style=="flower"){const int count=std::clamp(s.count+4,6,14);for(int i=0;i<count;++i){const float angle=2*Pi*i/count+progress*1.4f+rot;circle(add(center,mul(direction(angle),radius*.55f)),std::max(3.0f,s.size*.16f),.58f,true);}circle(center,std::max(3.0f,s.size*.12f),.9f,true);}
    else if(s.style=="diamond"){for(int i=0;i<std::min(s.count,8);++i)lines(regular(center,4,radius*(1-i*.035f),Pi/4),1-i*.08f,-1,true);}
    else if(s.style=="sonar"){const float heading=-.8f+progress*1.6f;lines({center,add(center,mul(direction(heading),radius))});for(int ring=1;ring<=std::clamp(s.count,2,6);++ring){std::vector<Vec2> arc;for(int i=0;i<=24;++i)arc.push_back(add(center,mul(direction(heading-.78f+1.56f*i/24),radius*ring/std::clamp(s.count,2,6))));lines(arc,.4f+.6f*ring/std::clamp(s.count,2,6),s.lineWidth*.7f);}}
    else if(s.style=="vortex"){const int arms=std::clamp(s.count,2,6);for(int arm=0;arm<arms;++arm){std::vector<Vec2> spiral;for(int i=0;i<=48;++i){const float t=float(i)/48,angle=arm*2*Pi/arms+t*Pi*2.4f+progress*2.2f;spiral.push_back(add(center,mul(direction(angle),radius*t)));}lines(spiral,1-arm*.07f,s.lineWidth*.72f);}}
    else if(s.style=="cross"){const float gap=radius*.24f;lines({{center.x-radius,center.y},{center.x-gap,center.y}});lines({{center.x+gap,center.y},{center.x+radius,center.y}});lines({{center.x,center.y-radius},{center.x,center.y-gap}});lines({{center.x,center.y+gap},{center.x,center.y+radius}});circle(center,gap,.7f);}
    else if(s.style=="confetti"){const int pieces=std::clamp(s.count*3,10,36);for(int i=0;i<pieces;++i){const float angle=2*Pi*i/pieces+rot+(deterministicRandom(i,event.variant,0)-.5f)*.62f;const Vec2 d=direction(angle),t{-d.y,d.x};const Vec2 point=add(center,mul(d,radius*(.38f+.62f*deterministicRandom(i,event.variant,1))));const float half=s.lineWidth+2;lines({sub(point,mul(t,half)),add(point,mul(t,half))},.7f+.1f*(i%3));}}
    else if(s.style=="heart"){std::vector<Vec2> heart;const float scale=radius/18;for(int i=0;i<64;++i){const float t=2*Pi*i/64,x=16*std::pow(std::sin(t),3),y=13*std::cos(t)-5*std::cos(2*t)-2*std::cos(3*t)-std::cos(4*t);heart.push_back({center.x+x*scale,center.y-y*scale});}lines(heart,1,-1,true);}
    else if(s.style=="ink"){circle(center,radius*.72f,.34f,true);for(int i=0;i<std::clamp(s.count+3,6,12);++i){const float angle=2*Pi*i/std::clamp(s.count+3,6,12)+rot+(deterministicRandom(i,event.variant,0)-.5f)*.4f;circle(add(center,mul(direction(angle),radius*.42f)),radius*(.28f+.05f*(i%3)),.22f,true);}circle(center,std::max(2.5f,radius*.12f),.68f,true);}
    else if(s.style=="splash"){circle(center,s.size*(.12f+.22f*std::sin(progress*Pi)),.46f,true);const int drops=std::clamp(s.count*2+4,8,24);for(int i=0;i<drops;++i){const float angle=2*Pi*i/drops+rot+(deterministicRandom(i,event.variant,0)-.5f)*.64f;const Vec2 point=add(center,mul(direction(angle),radius*(.45f+.55f*deterministicRandom(i,event.variant,1))));circle(point,std::max(2.0f,s.size*(.03f+.045f*deterministicRandom(i,event.variant,2))),.58f,true);}}
    else if(s.style=="eclipse"){circle(center,radius,.2f,true);circle(center,radius*.48f,.82f,true);const float angle=progress*Pi*2.2f-.8f+rot;drawCircle(overlay,add(center,mul(direction(angle),radius*.78f)),radius*.24f,alpha(lighter(color),a*.72f),1,true);circle(center,radius*1.15f,.48f);}
    else if(s.style=="nova"){drawPolygon(overlay,star(center,std::clamp(s.count+6,8,18),radius,radius*(.24f+.08f*progress),progress*.85f-Pi/2+rot),alpha(color,a*.34f),1,true);circle(center,std::max(3.0f,radius*.18f),.9f,true);}
    else if(s.style=="comet"){const Vec2 d=direction(-2.35f+progress*1.35f+rot),t{-d.y,d.x},head=add(center,mul(d,radius*.66f));for(int tail=0;tail<3;++tail)drawPolygon(overlay,{add(head,mul(t,s.size*(.17f-tail*.035f))),sub(head,mul(d,s.size*(.72f+tail*.18f)*easeOut(progress))),sub(head,mul(t,s.size*(.17f-tail*.035f)))},alpha(color,a*(.16f+.08f*(2-tail))),1,true);circle(head,std::max(3.0f,s.size*.2f),.9f,true);}
    else {const bool pixels=s.style=="pixelburst",bubbles=s.style=="bubbles"||s.style=="plasma",triangles=s.style=="prism"||s.style=="meteor";const int pieces=std::clamp(s.count*(pixels?3:2)+4,8,36);for(int i=0;i<pieces;++i){const float angle=2*Pi*i/pieces+rot+(deterministicRandom(i,event.variant,0)-.5f)*.62f;const Vec2 d=direction(angle),point=add(center,mul(d,radius*(.3f+.7f*deterministicRandom(i,event.variant,1))));const float unit=std::max(2.0f,s.size*(.035f+.04f*deterministicRandom(i,event.variant,2)));if(bubbles)circle(point,unit,.55f);else if(pixels)drawPolygon(overlay,{{point.x-unit,point.y-unit},{point.x+unit,point.y-unit},{point.x+unit,point.y+unit},{point.x-unit,point.y+unit}},alpha(color,a*.58f),1,true);else if(triangles){const Vec2 t{-d.y,d.x};drawPolygon(overlay,{add(point,mul(t,-unit)),add(point,mul(d,unit*2.4f)),add(point,mul(t,unit))},alpha(color,a*.45f),1,true);}else if(s.style=="lightning")lines({center,add(center,mul(d,radius*.35f)),add(center,mul(d,radius*.62f)),point},.85f,s.lineWidth*.8f);else lines({sub(point,mul(d,unit)),add(point,mul(d,unit))},.9f);}if(s.style=="plasma"||s.style=="meteor")circle(center,std::max(3.0f,radius*.14f),.8f,true);}
}

void RuntimeHost::drawTrail(Overlay &overlay,const TrailParticle&p,float progress){
    const auto&s=configuration_.settings;const float fade=(1-progress)*(1-progress),a=s.trailOpacity*fade,size=s.trailSize*p.scale*(.62f+.38f*fade);Color color=s.trailColor;if(s.trailStyle=="rainbow"){const float hue=std::fmod(p.serial*.047f+p.variant*.17f,1.0f),h=hue*6,x=1-std::abs(std::fmod(h,2.0f)-1);if(h<1)color={1,x,0,1};else if(h<2)color={x,1,0,1};else if(h<3)color={0,1,x,1};else if(h<4)color={0,x,1,1};else if(h<5)color={x,0,1,1};else color={1,0,x,1};}const Vec2 tangent{-p.direction.y,p.direction.x};
    if(s.trailStyle=="neon"||s.trailStyle=="laser"){drawPolygon(overlay,{sub(p.position,mul(p.direction,size*(s.trailStyle=="laser"?2.8f:2.2f))),add(p.position,mul(p.direction,size*.2f))},alpha(color,a*.22f),std::max(4.0f,size*.65f),false,false);drawPolygon(overlay,{sub(p.position,mul(p.direction,size*2.2f)),add(p.position,mul(p.direction,size*.2f))},alpha(lighter(color),a*.9f),std::max(1.0f,size*.16f),false,false);}
    else if(s.trailStyle=="cometTrail"||s.trailStyle=="fire"){drawPolygon(overlay,{add(p.position,mul(tangent,size*.5f)),sub(p.position,mul(p.direction,size*(s.trailStyle=="fire"?1.6f:3.2f))),sub(p.position,mul(tangent,size*.5f))},alpha(color,a*.4f),1,true);drawCircle(overlay,p.position,size*.5f,alpha(lighter(color),a*.9f),1,true);}
    else if(s.trailStyle=="stars"){drawPolygon(overlay,star(p.position,5,size,size*.42f,variantRotation(p.variant)),alpha(color,a*.76f),1,true);}
    else if(s.trailStyle=="hearts"){std::vector<Vec2> heart;const float scale=size/18;for(int i=0;i<30;++i){const float t=2*Pi*i/30,x=16*std::pow(std::sin(t),3),y=13*std::cos(t)-5*std::cos(2*t)-2*std::cos(3*t)-std::cos(4*t);heart.push_back({p.position.x+x*scale,p.position.y-y*scale});}drawPolygon(overlay,heart,alpha(color,a*.68f),1,true);}
    else if(s.trailStyle=="squares"||s.trailStyle=="diamonds"||s.trailStyle=="triangles"){const int sides=s.trailStyle=="triangles"?3:4;drawPolygon(overlay,regular(p.position,sides,size,s.trailStyle=="diamonds"?Pi/4:variantRotation(p.variant)),alpha(color,a*.75f),1,true);}
    else if(s.trailStyle=="ribbon"){drawPolygon(overlay,{sub(p.position,mul(p.direction,size*2)),add(p.position,mul(p.direction,size*.35f))},alpha(color,a*.68f),std::max(2.0f,size*.52f),false,false);}
    else if(s.trailStyle=="ice"||s.trailStyle=="sparks"){const int arms=s.trailStyle=="ice"?6:4;for(int i=0;i<arms;++i){const Vec2 d=direction(variantRotation(p.variant)+i*Pi/(arms/2));drawPolygon(overlay,{sub(p.position,mul(d,size*.78f)),add(p.position,mul(d,size*.78f))},alpha(lighter(color),a*.82f),std::max(1.0f,size*.12f),false,false);}}
    else if(s.trailStyle=="petalTrail"){for(int i=0;i<3;++i)drawCircle(overlay,add(p.position,mul(direction(variantRotation(p.variant)+2*Pi*i/3),size*.45f)),size*.35f,alpha(color,a*.5f),1,true);}
    else if(s.trailStyle=="pixels"){const float h=size*.5f;drawPolygon(overlay,{{p.position.x-h,p.position.y-h},{p.position.x+h,p.position.y-h},{p.position.x+h,p.position.y+h},{p.position.x-h,p.position.y+h}},alpha(color,a*.72f),1,true);}
    else if(s.trailStyle=="orbitTrail"){drawCircle(overlay,p.position,size*.35f,alpha(color,a*.7f),1,true);const float angle=progress*Pi*4+variantRotation(p.variant);drawCircle(overlay,add(p.position,mul(direction(angle),size)),size*.18f,alpha(color,a*.82f),1,true);}
    else if(s.trailStyle=="bubbleTrail"){drawCircle(overlay,p.position,size*.72f,alpha(color,a*.72f),std::max(1.0f,size*.13f),false);}
    else if(s.trailStyle=="smoke"){drawCircle(overlay,{p.position.x,p.position.y-progress*size*2.4f},size*(1.1f+progress),alpha(color,a*.1f),1,true);}
    else {drawCircle(overlay,p.position,size*(s.trailStyle=="soft"?1.0f:.54f),alpha(color,a*(s.trailStyle=="soft"?.28f:.82f)),1,true,s.trailGlow);}
}

void RuntimeHost::drawLabel(Overlay &overlay,const ClickEvent&event,float opacity){
    const wchar_t*names[]{L"Esquerdo",L"Meio",L"Direito"};std::wstring label=names[event.button];label+=event.pressed?L" \u2193":L" \u2191";std::wstring family=wide(configuration_.settings.font.substr(0,configuration_.settings.font.find(',')));if(family.empty())family=L"Segoe UI";ComHandle<IDWriteTextFormat> format;if(FAILED(writeFactory_->CreateTextFormat(family.c_str(),nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13,L"pt-BR",format.put())))return;ComHandle<ID2D1SolidColorBrush> brush;if(FAILED(d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,opacity),brush.put())))return;const Vec2 point=local(overlay,{float(event.position.x)+configuration_.settings.size+8,float(event.position.y)-9});const D2D1_RECT_F bounds{point.x,point.y,point.x+180,point.y+32};d2dContext_->DrawTextW(label.c_str(),static_cast<UINT32>(label.size()),format.get(),bounds,brush.get());
}

int RuntimeHost::run(){MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);}

} // namespace rc
