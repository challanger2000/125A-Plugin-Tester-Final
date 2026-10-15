#pragma once
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include <string>
#include <vector>

// Non-invasive to host DSP source; instruments imports *in the tested DLL*
// only. Does not claim detection of statically linked CRT allocation, direct
// syscalls, indirect GetProcAddress functions or internal custom allocators.
namespace CallbackImportProbe {
inline thread_local bool active = false;
inline thread_local uint64_t heapCalls = 0;
inline thread_local uint64_t blockingCalls = 0;
using HeapAllocFn = LPVOID (WINAPI*)(HANDLE,DWORD,SIZE_T);
using HeapReAllocFn = LPVOID (WINAPI*)(HANDLE,DWORD,LPVOID,SIZE_T);
using VirtualAllocFn = LPVOID (WINAPI*)(LPVOID,SIZE_T,DWORD,DWORD);
using EnterCSFn = void (WINAPI*)(LPCRITICAL_SECTION);
using WaitFn = DWORD (WINAPI*)(HANDLE,DWORD);
using WaitExFn = DWORD (WINAPI*)(HANDLE,DWORD,BOOL);
inline HeapAllocFn originalHeapAlloc = nullptr;
inline HeapReAllocFn originalHeapReAlloc = nullptr;
inline VirtualAllocFn originalVirtualAlloc = nullptr;
inline EnterCSFn originalEnterCS = nullptr;
inline WaitFn originalWait = nullptr;
inline WaitExFn originalWaitEx = nullptr;
inline LPVOID WINAPI hookedHeapAlloc(HANDLE h,DWORD f,SIZE_T n) {
    if (active) ++heapCalls;
    return originalHeapAlloc(h,f,n);
}
inline LPVOID WINAPI hookedHeapReAlloc(HANDLE h,DWORD f,LPVOID p,SIZE_T n) {
    if (active) ++heapCalls;
    return originalHeapReAlloc(h,f,p,n);
}
inline LPVOID WINAPI hookedVirtualAlloc(LPVOID p,SIZE_T n,DWORD t,DWORD f) {
    if (active) ++heapCalls;
    return originalVirtualAlloc(p,n,t,f);
}
inline void WINAPI hookedEnterCS(LPCRITICAL_SECTION p) {
    if (active) ++blockingCalls; // attempted blocking API, not proven contention
    originalEnterCS(p);
}
inline DWORD WINAPI hookedWait(HANDLE h,DWORD timeout) {
    if (active && timeout != 0) ++blockingCalls;
    return originalWait(h,timeout);
}
inline DWORD WINAPI hookedWaitEx(HANDLE h,DWORD timeout,BOOL alertable) {
    if (active && timeout != 0) ++blockingCalls;
    return originalWaitEx(h,timeout,alertable);
}
struct Hook {
    const char* name;
    void* replacement;
    void** original;
};
inline std::array<Hook,6> apiHooks{{
    {"HeapAlloc",reinterpret_cast<void*>(&hookedHeapAlloc),reinterpret_cast<void**>(&originalHeapAlloc)},
    {"HeapReAlloc",reinterpret_cast<void*>(&hookedHeapReAlloc),reinterpret_cast<void**>(&originalHeapReAlloc)},
    {"VirtualAlloc",reinterpret_cast<void*>(&hookedVirtualAlloc),reinterpret_cast<void**>(&originalVirtualAlloc)},
    {"EnterCriticalSection",reinterpret_cast<void*>(&hookedEnterCS),reinterpret_cast<void**>(&originalEnterCS)},
    {"WaitForSingleObject",reinterpret_cast<void*>(&hookedWait),reinterpret_cast<void**>(&originalWait)},
    {"WaitForSingleObjectEx",reinterpret_cast<void*>(&hookedWaitEx),reinterpret_cast<void**>(&originalWaitEx)}
}};
struct Patch { void** location; void* saved; };
struct Results { uint64_t allocations=0, blockingApis=0; size_t patchedImports=0; bool active=false; };
class ImportedApis {
public:
    explicit ImportedApis(const std::wstring& name) {
        // All VST3 modules can use forwarded KERNELBASE imports. Patch by
        // import name in the plug-in's IAT, without rewriting code sections.
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
        if(snapshot==INVALID_HANDLE_VALUE) return;
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        std::wstring needle=name;
        std::transform(needle.begin(),needle.end(),needle.begin(),[](wchar_t c){return std::towlower(c);});
        for(BOOL ok=Module32FirstW(snapshot,&entry);ok;ok=Module32NextW(snapshot,&entry)) {
            std::wstring full(entry.szExePath);
            std::transform(full.begin(),full.end(),full.begin(),[](wchar_t c){return std::towlower(c);});
            if(full.find(needle)==std::wstring::npos) continue;
            patch(entry.hModule);
        }
        CloseHandle(snapshot);
    }
    ~ImportedApis() {
        for(auto it=patches_.rbegin();it!=patches_.rend();++it) {
            DWORD previous=0;
            if(VirtualProtect(it->location,sizeof(void*),PAGE_READWRITE,&previous)) {
                *it->location=it->saved;
                DWORD ignored=0;VirtualProtect(it->location,sizeof(void*),previous,&ignored);
            }
        }
    }
    ImportedApis(const ImportedApis&)=delete;
    ImportedApis& operator=(const ImportedApis&)=delete;
    size_t covered() const{return patches_.size();}
private:
    std::vector<Patch> patches_;
    void patch(HMODULE module) {
        auto* base=reinterpret_cast<unsigned char*>(module);
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE) return;
        auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(!dir.VirtualAddress) return;
        auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);
        for(;imports->Name;++imports) {
            if(!imports->OriginalFirstThunk) continue;
            auto* orig=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);
            auto* iat=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
            for(;orig->u1.AddressOfData;++orig,++iat) {
                if(IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))continue;
                auto* named=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+orig->u1.AddressOfData);
                for(auto& h:apiHooks) {
                    if(std::strcmp(reinterpret_cast<const char*>(named->Name),h.name)!=0)continue;
                    auto* slot=reinterpret_cast<void**>(&iat->u1.Function);
                    void* prior=*slot;
                    if(prior==h.replacement)continue;
                    DWORD old=0;
                    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))continue;
                    *h.original=prior;
                    *slot=h.replacement;
                    DWORD ignored=0;VirtualProtect(slot,sizeof(void*),old,&ignored);
                    patches_.push_back({slot,prior});
                    break;
                }
            }
        }
    }
};
class Scope {
public:
    explicit Scope(bool enabled):enabled_(enabled){if(enabled_){heapCalls=0;blockingCalls=0;active=true;}}
    ~Scope(){active=false;}
    Results finish(size_t imports) {
        active=false;
        return {heapCalls,blockingCalls,imports,enabled_};
    }
private: bool enabled_;
};
} // namespace CallbackImportProbe
#endif
