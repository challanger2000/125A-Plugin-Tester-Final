#include <windows.h>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fobject.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "report_paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

constexpr int kEditorCycles = 5;
constexpr wchar_t kWindowClassName[] = L"125A_Plugin_Tester_EditorLifecycleHost";

LRESULT CALLBACK hostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool ensureWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&wc) != 0)
        return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void pumpMessages(DWORD durationMs) {
    const ULONGLONG end = GetTickCount64() + durationMs;
    MSG msg{};
    do {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}

class HostPlugFrame final : public FObject, public IPlugFrame {
public:
    explicit HostPlugFrame(HWND hwnd) : hwnd_(hwnd) {}

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize || !hwnd_)
            return kInvalidArgument;

        const int width = std::max<int>(1, newSize->right - newSize->left);
        const int height = std::max<int>(1, newSize->bottom - newSize->top);

        RECT outer{0, 0, width, height};
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        if (AdjustWindowRectEx(&outer, style, FALSE, exStyle)) {
            SetWindowPos(hwnd_,
                         nullptr,
                         0,
                         0,
                         outer.right - outer.left,
                         outer.bottom - outer.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        return view->onSize(newSize);
    }

    OBJ_METHODS(HostPlugFrame, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugFrame)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)

private:
    HWND hwnd_ = nullptr;
};

struct ControllerHolder {
    IEditController* controller = nullptr;
    IConnectionPoint* componentCP = nullptr;
    IConnectionPoint* controllerCP = nullptr;
    bool initializedSeparately = false;
    bool connected = false;

    void close() {
        if (connected && componentCP && controllerCP) {
            componentCP->disconnect(controllerCP);
            controllerCP->disconnect(componentCP);
            connected = false;
        }
        if (componentCP) {
            componentCP->release();
            componentCP = nullptr;
        }
        if (controllerCP) {
            controllerCP->release();
            controllerCP = nullptr;
        }
        if (!controller)
            return;
        if (initializedSeparately)
            controller->terminate();
        controller->release();
        controller = nullptr;
        initializedSeparately = false;
    }

    ~ControllerHolder() { close(); }
};

bool acquireController(IComponent* component,
                       const PluginFactory& factory,
                       FUnknown* hostContext,
                       ControllerHolder& holder,
                       std::string& error) {
    if (component->queryInterface(IEditController::iid,
                                  reinterpret_cast<void**>(&holder.controller)) == kResultTrue &&
        holder.controller) {
        return true;
    }

    TUID controllerCID{};
    if (component->getControllerClassId(controllerCID) != kResultTrue) {
        error = "component exposes no edit controller class";
        return false;
    }

    auto separate = factory.createInstance<IEditController>(VST3::UID(controllerCID));
    if (!separate) {
        error = "edit controller creation failed";
        return false;
    }

    holder.controller = separate.take();
    if (holder.controller->initialize(hostContext) != kResultOk) {
        holder.controller->release();
        holder.controller = nullptr;
        error = "edit controller initialize failed";
        return false;
    }

    holder.initializedSeparately = true;

    const bool componentHasCP =
        component->queryInterface(IConnectionPoint::iid,
                                  reinterpret_cast<void**>(&holder.componentCP)) == kResultTrue &&
        holder.componentCP;
    const bool controllerHasCP =
        holder.controller->queryInterface(IConnectionPoint::iid,
                                          reinterpret_cast<void**>(&holder.controllerCP)) == kResultTrue &&
        holder.controllerCP;

    if (componentHasCP && controllerHasCP) {
        const tresult c2e = holder.componentCP->connect(holder.controllerCP);
        const tresult e2c = holder.controllerCP->connect(holder.componentCP);
        if (c2e == kResultTrue && e2c == kResultTrue) {
            holder.connected = true;
        } else {
            if (c2e == kResultTrue)
                holder.componentCP->disconnect(holder.controllerCP);
            if (e2c == kResultTrue)
                holder.controllerCP->disconnect(holder.componentCP);
        }
    }

    return true;
}

class StageReport {
public:
    explicit StageReport(const fs::path& pluginPath)
    : out_(ReportPaths::editorLifecycle(pluginPath), std::ios::binary | std::ios::trunc) {
        if (out_) {
            out_ << "125A Plugin Tester / Editor Lifecycle Probe\n";
            out_ << "Version: 0.2.7\n";
            out_ << "Plugin: " << pluginPath.string() << "\n\n";
            out_.flush();
        }
    }

    void mark(const std::string& text) {
        if (!out_)
            return;
        out_ << "Stage: " << text << "\n";
        out_.flush();
    }

    void result(const std::string& text) {
        if (!out_)
            return;
        out_ << "Result: " << text << "\n";
        out_.flush();
    }

private:
    std::ofstream out_;
};

int runEditorLifecycle(const fs::path& pluginPath) {
    std::error_code reportEc;
    if (!ReportPaths::ensureRoot(reportEc))
        return 2;
    StageReport stage(pluginPath);
    stage.mark("module load");

    std::string loadError;
    auto module = Module::create(pluginPath.u8string(), loadError);
    if (!module) {
        stage.result("FAIL - module load");
        std::cerr << "[FAIL] Editor lifecycle - module load failed: "
                  << (loadError.empty() ? "unknown error" : loadError) << '\n';
        return 1;
    }

    stage.mark("native host window class");
    if (!ensureWindowClass()) {
        stage.result("FAIL - native host window class");
        std::cerr << "[FAIL] Editor lifecycle - native host window class registration failed\n";
        return 1;
    }

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);

    int audioClasses = 0;
    int editorClasses = 0;

    for (const auto& classInfo : factory.classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass &&
            std::string(classInfo.category().data()) != "Audio Mix Processor")
            continue;

        ++audioClasses;
        stage.mark(std::string("class ") + classInfo.name().data() + " - component create");
        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            std::cerr << "[FAIL] Editor lifecycle - component creation failed for "
                      << classInfo.name().data() << '\n';
            return 1;
        }

        stage.mark(std::string("class ") + classInfo.name().data() + " - component initialize");
        if (component->initialize(hostContext) != kResultOk) {
            std::cerr << "[FAIL] Editor lifecycle - component initialize failed for "
                      << classInfo.name().data() << '\n';
            return 1;
        }

        ControllerHolder controller;
        std::string controllerError;
        stage.mark(std::string("class ") + classInfo.name().data() + " - controller acquire/connect");
        if (!acquireController(component.get(), factory, hostContext, controller, controllerError)) {
            component->terminate();
            std::cout << "[INFO] Editor lifecycle - " << classInfo.name().data()
                      << " has no usable edit controller (" << controllerError << ")\n";
            continue;
        }

        bool classHasEditor = false;
        for (int cycle = 1; cycle <= kEditorCycles; ++cycle) {
            const std::string cyclePrefix = std::string("class ") + classInfo.name().data() +
                                            " - cycle " + std::to_string(cycle) + " - ";
            stage.mark(cyclePrefix + "createView");
            IPlugView* view = controller.controller->createView(ViewType::kEditor);
            if (!view) {
                if (cycle == 1) {
                    std::cout << "[INFO] Editor lifecycle - " << classInfo.name().data()
                              << " provides no editor view\n";
                    break;
                }
                std::cerr << "[FAIL] Editor lifecycle - createView(kEditor) returned null on cycle "
                          << cycle << " after a previous editor cycle succeeded for "
                          << classInfo.name().data() << '\n';
                controller.close();
                component->terminate();
                return 1;
            }

            if (!classHasEditor) {
                classHasEditor = true;
                ++editorClasses;
            }

            stage.mark(cyclePrefix + "getSize");
            ViewRect rect{};
            if (view->getSize(&rect) != kResultTrue) {
                rect.left = 0;
                rect.top = 0;
                rect.right = 640;
                rect.bottom = 480;
            }

            const int width = std::max<int>(1, rect.right - rect.left);
            const int height = std::max<int>(1, rect.bottom - rect.top);
            RECT outer{0, 0, width, height};
            const DWORD windowStyle = WS_OVERLAPPEDWINDOW;
            const DWORD windowExStyle = WS_EX_TOOLWINDOW;
            AdjustWindowRectEx(&outer, windowStyle, FALSE, windowExStyle);

            stage.mark(cyclePrefix + "create HWND");
            HWND hwnd = CreateWindowExW(
                windowExStyle,
                kWindowClassName,
                L"125A Editor Lifecycle Host",
                windowStyle,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                outer.right - outer.left,
                outer.bottom - outer.top,
                nullptr,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);

            if (!hwnd) {
                view->release();
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - native HWND creation failed on cycle "
                          << cycle << '\n';
                return 1;
            }

            stage.mark(cyclePrefix + "platform support");
            if (view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
                DestroyWindow(hwnd);
                view->release();
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - editor does not support HWND platform type\n";
                return 1;
            }

            auto* plugFrame = new HostPlugFrame(hwnd);
            stage.mark(cyclePrefix + "setFrame");
            const tresult frameResult = view->setFrame(plugFrame);
            if (frameResult != kResultTrue) {
                plugFrame->release();
                DestroyWindow(hwnd);
                view->release();
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - setFrame(IPlugFrame) failed on cycle "
                          << cycle << " for " << classInfo.name().data() << '\n';
                return 1;
            }

            stage.mark(cyclePrefix + "attached");
            const tresult attachResult = view->attached(reinterpret_cast<void*>(hwnd), kPlatformTypeHWND);
            if (attachResult != kResultTrue) {
                view->setFrame(nullptr);
                plugFrame->release();
                DestroyWindow(hwnd);
                view->release();
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - attached(HWND) failed on cycle "
                          << cycle << " for " << classInfo.name().data() << '\n';
                return 1;
            }

            stage.mark(cyclePrefix + "runtime/message pump");
            ShowWindow(hwnd, SW_SHOWNA);
            UpdateWindow(hwnd);
            pumpMessages(150);

            stage.mark(cyclePrefix + "focus");
            view->onFocus(true);
            pumpMessages(30);
            view->onFocus(false);

            stage.mark(cyclePrefix + "removed");
            const tresult removeResult = view->removed();
            stage.mark(cyclePrefix + "setFrame(nullptr)");
            const tresult clearFrameResult = view->setFrame(nullptr);
            plugFrame->release();
            pumpMessages(30);
            stage.mark(cyclePrefix + "destroy HWND");
            DestroyWindow(hwnd);
            pumpMessages(20);
            stage.mark(cyclePrefix + "release view");
            view->release();
            stage.mark(cyclePrefix + "complete");

            if (removeResult != kResultTrue || clearFrameResult != kResultTrue) {
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - detach/frame cleanup failed on cycle "
                          << cycle << " for " << classInfo.name().data() << '\n';
                return 1;
            }

            std::cout << "[PASS] Editor lifecycle cycle " << cycle << " - "
                      << classInfo.name().data()
                      << " create/attach/runtime/detach/destroy completed\n";
        }

        stage.mark(std::string("class ") + classInfo.name().data() + " - controller disconnect/terminate");
        controller.close();
        stage.mark(std::string("class ") + classInfo.name().data() + " - component terminate");
        if (component->terminate() != kResultOk) {
            std::cerr << "[FAIL] Editor lifecycle - component terminate failed for "
                      << classInfo.name().data() << '\n';
            return 1;
        }
    }

    if (audioClasses == 0) {
        std::cout << "[INFO] Editor lifecycle - no supported audio processor class found\n";
        return 10;
    }

    if (editorClasses == 0) {
        std::cout << "[INFO] Editor lifecycle - no editor supplied; test not applicable\n";
        return 10;
    }

    stage.result("PASS");
    std::cout << "[PASS] Editor lifecycle / GUI runtime - " << kEditorCycles
              << " open/runtime/close cycles completed for each editor class\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "[FAIL] Editor lifecycle - no VST3 path supplied\n";
        return 2;
    }

    const fs::path pluginPath = fs::u8path(argv[1]);
    if (!fs::exists(pluginPath)) {
        std::cerr << "[FAIL] Editor lifecycle - path does not exist\n";
        return 2;
    }

    const HRESULT comResult = CoInitialize(nullptr);
    if (FAILED(comResult)) {
        std::cerr << "[FAIL] Editor lifecycle - COM initialization failed\n";
        return 2;
    }

    const int result = runEditorLifecycle(pluginPath);
    CoUninitialize();
    return result;
}
