#include <windows.h>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fobject.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "report_paths.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>
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

// Run the editor's message loop while a separate worker thread submits
// audio blocks to the same live plugin instance. No GUI calls are made on the
// audio thread. Returns 1=verified, 0=not applicable, -1=processing failure.
int pumpWithConcurrentAudio(IComponent* component, DWORD runtimeMs) {
    if (!component ||
        component->getBusCount(kAudio, kInput) != 1 ||
        component->getBusCount(kAudio, kOutput) != 1) {
        pumpMessages(runtimeMs);
        return 0;
    }
    BusInfo inputInfo{}, outputInfo{};
    if (component->getBusInfo(kAudio, kInput, 0, inputInfo) != kResultTrue ||
        component->getBusInfo(kAudio, kOutput, 0, outputInfo) != kResultTrue ||
        inputInfo.channelCount < 1 || inputInfo.channelCount > 2 ||
        outputInfo.channelCount < 1 || outputInfo.channelCount > 2) {
        pumpMessages(runtimeMs);
        return 0;
    }
    IAudioProcessor* processor = nullptr;
    if (component->queryInterface(IAudioProcessor::iid,
                                   reinterpret_cast<void**>(&processor)) != kResultTrue ||
        !processor) {
        pumpMessages(runtimeMs);
        return 0;
    }
    const auto release = [&]() { processor->release(); };
    if (processor->canProcessSampleSize(kSample32) != kResultTrue) {
        release();
        pumpMessages(runtimeMs);
        return 0;
    }
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = 48000.0;
    setup.maxSamplesPerBlock = 256;
    if (processor->setupProcessing(setup) != kResultTrue) {
        release();
        pumpMessages(runtimeMs);
        return 0;
    }
    if (component->activateBus(kAudio, kInput, 0, true) != kResultTrue ||
        component->activateBus(kAudio, kOutput, 0, true) != kResultTrue) {
        release();
        pumpMessages(runtimeMs);
        return 0;
    }

    constexpr int samples = 256;
    std::vector<std::vector<Sample32>> inputData(
        static_cast<size_t>(inputInfo.channelCount), std::vector<Sample32>(samples, 0.1f));
    std::vector<std::vector<Sample32>> outputData(
        static_cast<size_t>(outputInfo.channelCount), std::vector<Sample32>(samples));
    std::vector<Sample32*> inputPtrs, outputPtrs;
    for (auto& channel : inputData) inputPtrs.push_back(channel.data());
    for (auto& channel : outputData) outputPtrs.push_back(channel.data());

    AudioBusBuffers inputBus{}, outputBus{};
    inputBus.numChannels = inputInfo.channelCount;
    inputBus.channelBuffers32 = inputPtrs.data();
    outputBus.numChannels = outputInfo.channelCount;
    outputBus.channelBuffers32 = outputPtrs.data();
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = samples;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;

    if (component->setActive(true) != kResultTrue) {
        release();
        return -1;
    }
    if (processor->setProcessing(true) != kResultTrue) {
        component->setActive(false);
        release();
        return -1;
    }
    std::atomic<bool> running{true};
    std::atomic<bool> failed{false};
    std::atomic<int> processed{0};
    std::thread audio([&] {
        while (running.load(std::memory_order_relaxed)) {
            for (auto& channel : outputData)
                std::fill(channel.begin(), channel.end(), Sample32(0));
            if (processor->process(data) != kResultOk) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            for (const auto& channel : outputData)
                for (const auto value : channel)
                    if (!std::isfinite(value))
                        failed.store(true, std::memory_order_relaxed);
            if (failed.load(std::memory_order_relaxed)) break;
            processed.fetch_add(1, std::memory_order_relaxed);
            Sleep(5); // pacing is outside the real-time process() callback
        }
    });
    pumpMessages(runtimeMs);
    running.store(false, std::memory_order_relaxed);
    audio.join();
    const bool stopped = processor->setProcessing(false) == kResultTrue;
    const bool inactive = component->setActive(false) == kResultTrue;
    release();
    return !failed.load() && stopped && inactive && processed.load() > 0 ? 1 : -1;
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
            out_ << "Version: 0.2.10\n";
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
            if (controllerError == "component exposes no edit controller class") {
                std::cout << "[INFO] Editor lifecycle - " << classInfo.name().data()
                          << " has no edit controller\n";
                continue;
            }
            std::cerr << "[FAIL] Editor lifecycle - " << classInfo.name().data()
                      << ": " << controllerError << '\n';
            return 1;
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

            stage.mark(cyclePrefix + "runtime/message pump + concurrent audio");
            ShowWindow(hwnd, SW_SHOWNA);
            UpdateWindow(hwnd);
            const int concurrentAudio = pumpWithConcurrentAudio(component.get(), 150);
            if (concurrentAudio > 0)
                std::cout << "[PASS] Editor/audio concurrency - processed audio while editor was attached\n";
            else if (concurrentAudio == 0)
                std::cout << "[INFO] Editor/audio concurrency - unsupported bus layout or sample format\n";

            stage.mark(cyclePrefix + "content scale 100/150 percent");
            bool scaleGeometryOk = true;
            IPlugViewContentScaleSupport* scaleSupport = nullptr;
            if (view->queryInterface(IPlugViewContentScaleSupport::iid,
                                     reinterpret_cast<void**>(&scaleSupport)) == kResultTrue &&
                scaleSupport) {
                const tresult scaled = scaleSupport->setContentScaleFactor(1.5f);
                ViewRect scaledRect{};
                const bool readable = view->getSize(&scaledRect) == kResultTrue;
                const tresult restored = scaleSupport->setContentScaleFactor(1.0f);
                if (scaled == kResultTrue && restored == kResultTrue) {
                    scaleGeometryOk = readable && scaledRect.right > scaledRect.left &&
                                      scaledRect.bottom > scaledRect.top;
                    std::cout << (scaleGeometryOk ? "[PASS]" : "[FAIL]")
                              << " Editor scale 100/150 - supported factor calls and valid geometry\n";
                } else {
                    std::cout << "[INFO] Editor scale 100/150 - exposed interface declined optional scaling\n";
                }
                scaleSupport->release();
            } else {
                std::cout << "[INFO] Editor scale 100/150 - optional content scale interface unavailable\n";
            }

            stage.mark(cyclePrefix + "minimize/restore");
            ShowWindow(hwnd, SW_MINIMIZE);
            pumpMessages(30);
            ShowWindow(hwnd, SW_RESTORE);
            pumpMessages(30);

            bool geometryOk = true;
            if (view->canResize() == kResultTrue) {
                stage.mark(cyclePrefix + "resizable onSize roundtrip");
                ViewRect original{};
                if (view->getSize(&original) == kResultTrue) {
                    ViewRect resized = original;
                    resized.right += 32;
                    resized.bottom += 24;
                    if (view->checkSizeConstraint(&resized) == kResultTrue) {
                        if (view->onSize(&resized) != kResultTrue ||
                            view->onSize(&original) != kResultTrue)
                            geometryOk = false;
                    }
                }
            }

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

            if (!scaleGeometryOk) {
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - invalid geometry after accepted content scaling\n";
                return 1;
            }
            if (!geometryOk) {
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor lifecycle - supported resize/onSize roundtrip failed\n";
                return 1;
            }
            if (concurrentAudio < 0) {
                controller.close();
                component->terminate();
                std::cerr << "[FAIL] Editor/audio concurrency - processing failed with attached editor\n";
                return 1;
            }

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
