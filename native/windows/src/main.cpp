#include "renderer.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {
constexpr wchar_t PipeName[] = L"\\\\.\\pipe\\LOCAL\\RadiantCursor.Runtime";
constexpr wchar_t MutexName[] = L"Local\\RadiantCursor.Runtime.Singleton";

std::filesystem::path defaultDataDirectory() {
    wchar_t buffer[32768]{};
    const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,static_cast<DWORD>(std::size(buffer)));
    if(size>0&&size<std::size(buffer))return std::filesystem::path(buffer)/L"RadiantCursor";
    const DWORD fallback=GetEnvironmentVariableW(L"USERPROFILE",buffer,static_cast<DWORD>(std::size(buffer)));
    return (fallback>0&&fallback<std::size(buffer)?std::filesystem::path(buffer):std::filesystem::temp_directory_path())/L"AppData"/L"Local"/L"RadiantCursor";
}

std::filesystem::path parseDataDirectory() {
    int count=0;LPWSTR *arguments=CommandLineToArgvW(GetCommandLineW(),&count);std::filesystem::path result=defaultDataDirectory();
    if(arguments){for(int index=1;index+1<count;++index)if(std::wstring_view(arguments[index])==L"--data-dir"){result=arguments[index+1];break;}LocalFree(arguments);}return std::filesystem::absolute(result).lexically_normal();
}

bool hasArgument(std::wstring_view expected) {
    int count=0;LPWSTR *arguments=CommandLineToArgvW(GetCommandLineW(),&count);bool found=false;
    if(arguments){for(int index=1;index<count;++index)if(std::wstring_view(arguments[index])==expected){found=true;break;}LocalFree(arguments);}return found;
}

void log(const std::filesystem::path &dataDirectory,std::string_view message){
    try{std::filesystem::create_directories(dataDirectory/L"logs");std::ofstream stream(dataDirectory/L"logs"/L"runtime.log",std::ios::app|std::ios::binary);SYSTEMTIME time{};GetLocalTime(&time);stream<<time.wYear<<'-'<<time.wMonth<<'-'<<time.wDay<<' '<<time.wHour<<':'<<time.wMinute<<':'<<time.wSecond<<" "<<message<<'\n';}catch(...){}
}

void servePipe(HWND controller,std::atomic_bool &stopping,const std::filesystem::path &dataDirectory){
    while(!stopping.load()){
        HANDLE pipe=CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1024,1024,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE){log(dataDirectory,"CreateNamedPipe failed");Sleep(500);continue;}
        const BOOL connected=ConnectNamedPipe(pipe,nullptr)?TRUE:GetLastError()==ERROR_PIPE_CONNECTED;
        if(connected){char input[256]{};DWORD read=0;if(ReadFile(pipe,input,sizeof(input)-1,&read,nullptr)&&read){std::string command(input,input+read);while(!command.empty()&&(command.back()=='\r'||command.back()=='\n'||command.back()=='\0'))command.pop_back();std::string reply="ERR unknown command\n";if(command=="PING")reply="OK PONG\n";else if(command=="RELOAD"){DWORD_PTR loaded=0;const LRESULT delivered=SendMessageTimeoutW(controller,rc::RuntimeReloadMessage,0,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,3000,&loaded);reply=delivered&&loaded?"OK RELOADED\n":"ERR configuração inválida\n";}else if(command=="STOP"){stopping.store(true);PostMessageW(controller,rc::RuntimeStopMessage,0,0);reply="OK STOPPING\n";}DWORD written=0;WriteFile(pipe,reply.data(),static_cast<DWORD>(reply.size()),&written,nullptr);FlushFileBuffers(pipe);}DisconnectNamedPipe(pipe);}CloseHandle(pipe);
    }
}

bool sendPipeCommand(const char *command){if(!WaitNamedPipeW(PipeName,1500))return false;HANDLE pipe=CreateFileW(PipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(pipe==INVALID_HANDLE_VALUE)return false;DWORD written=0;const BOOL sent=WriteFile(pipe,command,static_cast<DWORD>(std::char_traits<char>::length(command)),&written,nullptr);CloseHandle(pipe);return sent==TRUE;}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    if(hasArgument(L"--stop"))return sendPipeCommand("STOP\n")?0:1;
    HANDLE singleton=CreateMutexW(nullptr,TRUE,MutexName);if(!singleton)return 2;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(singleton);return 0;}
    const auto dataDirectory=parseDataDirectory();log(dataDirectory,"runtime starting");CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    rc::RuntimeHost host(dataDirectory);std::wstring error;if(!host.initialize(error)){log(dataDirectory,"runtime initialization failed");MessageBoxW(nullptr,error.c_str(),L"RadiantCursor Runtime",MB_OK|MB_ICONERROR);CoUninitialize();ReleaseMutex(singleton);CloseHandle(singleton);return 1;}
    std::atomic_bool stopping=false;std::thread pipeThread(servePipe,host.controlWindow(),std::ref(stopping),std::cref(dataDirectory));const int result=host.run();stopping.store(true);sendPipeCommand("PING\n");if(pipeThread.joinable())pipeThread.join();log(dataDirectory,"runtime stopped");CoUninitialize();ReleaseMutex(singleton);CloseHandle(singleton);return result;
}
