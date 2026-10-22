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

bool ensureWindowClass(DWORD& errorCode) {
    errorCode = ERROR_SUCCESS;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&wc) != 0)
        return true;
    errorCode = GetLastError(); // capture before any report I/O
    return errorCode == ERROR_CLASS_ALREADY_EXISTS;
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

// Minimal, lifetime-bound host implementation. Not IComponentHandler2.
class EditorHostComponentHandler final : public FObject, public IComponentHandler {
public:
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    OBJ_METHODS(EditorHostComponentHandler, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

struct ControllerHolder {
    IEditController* controller = nullptr;
    IConnectionPoint* componentCP = nullptr;
    IConnectionPoint* controllerCP = nullptr;
    bool initializedSeparately = false;
    bool connected = false;
    EditorHostComponentHandler* hostHandler = nullptr;

    void close() {
        if (hostHandler) {
            if (controller)
                controller->setComponentHandler(nullptr);
            hostHandler->release();
            hostHandler = nullptr;
        }
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

void registerEditorHostHandler(ControllerHolder& holder) {
    if (!holder.controller) return;
    auto* handler = new EditorHostComponentHandler();
    if (holder.controller->setComponentHandler(handler) == kResultOk) {
        holder.hostHandler = handler;
        std::cout << "[PASS] Editor host simulation - IComponentHandler installed on controller thread\n";
    } else {
        handler->release();
        std::cout << "[INFO] Editor host simulation - controller declined handler; no UI semantics inferred\n";
    }
}

bool acquireController(IComponent* component,
                       const PluginFactory& factory,
                       FUnknown* hostContext,
                       ControllerHolder& holder,
                       std::string& error) {
    if (component->queryInterface(IEditController::iid,
                                  reinterpret_cast<void**>(&holder.controller)) == kResultTrue &&
        holder.controller) {
        registerEditorHostHandler(holder);
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
    registerEditorHostHandler(holder);

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


// Result 1 = controlled oracle verified/NOT TESTED, 0 = plugin fixture
// contract failed, 2 = host/Win32 resource failure (INCONCLUSIVE).
int runControlledPeerViewOracle(IEditController* controller, const std::string& className) {
    if (className != "125A QA Good Peer Views" &&
        className != "125A QA Bad Peer Views" &&
        className != "125A QA Bad Peer Cleanup") {
        std::cout << "[INFO] SA01 peer-view notification - NOT TESTED: "
                     "no general independent visual control/repaint oracle for this plugin\n";
        return true;
    }
    if (!controller) {
        std::cerr << "[FAIL] SA01 controlled peer-view oracle - controller missing\n";
        return false;
    }
    struct Peer {
        IPlugView* view = nullptr;
        HWND hwnd = nullptr;
        HostPlugFrame* frame = nullptr;
        bool attached = false;
    };
    Peer peers[2]{};
    const auto cleanup = [&]() {
        bool pluginClean = true;
        bool hostClean = true;
        for (auto& peer : peers) {
            if (peer.view) {
                if (peer.attached && peer.view->removed() != kResultTrue)
                    pluginClean = false;
                if (peer.frame && peer.view->setFrame(nullptr) != kResultTrue)
                    pluginClean = false;
                peer.attached = false;
            }
            if (peer.frame) peer.frame->release();
            if (peer.hwnd && !DestroyWindow(peer.hwnd)) {
                const DWORD code = GetLastError();
                std::cerr << "[INFO] SA01 host cleanup INCONCLUSIVE - DestroyWindow failed Win32="
                          << code << "\n";
                hostClean = false;
            }
            if (peer.view) peer.view->release();
            peer = {};
        }
        // Return 2 for host failure, 0 for plugin contract failure, 1 OK.
        return !hostClean ? 2 : (pluginClean ? 1 : 0);
    };

    bool attachedAll = true;
    bool hostFailure = false;
    for (auto& peer : peers) {
        peer.view = controller->createView(ViewType::kEditor);
        if (!peer.view || peer.view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            attachedAll = false;
            break;
        }
        ViewRect size{};
        if (peer.view->getSize(&size) != kResultTrue ||
            size.right <= size.left || size.bottom <= size.top) {
            attachedAll = false;
            break;
        }
        const DWORD style = WS_OVERLAPPEDWINDOW, exStyle = WS_EX_TOOLWINDOW;
        RECT outer{0,0,size.right-size.left,size.bottom-size.top};
        AdjustWindowRectEx(&outer, style, FALSE, exStyle);
        peer.hwnd = CreateWindowExW(exStyle, kWindowClassName,
                                    L"125A SA01 Peer View Host", style,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    outer.right-outer.left, outer.bottom-outer.top,
                                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!peer.hwnd) {
            hostFailure = true;
            attachedAll = false;
            const DWORD createError = GetLastError();
            std::cerr << "[INFO] SA01 native host CreateWindowExW failed, Win32=" << createError << "\n";
            break;
        }
        peer.frame = new HostPlugFrame(peer.hwnd);
        if (peer.view->setFrame(peer.frame) != kResultTrue ||
            peer.view->attached(reinterpret_cast<void*>(peer.hwnd), kPlatformTypeHWND) != kResultTrue) {
            attachedAll = false;
            break;
        }
        peer.attached = true;
        ShowWindow(peer.hwnd, SW_SHOWNA);
        UpdateWindow(peer.hwnd);
    }
    if (!attachedAll) {
        const int cleanupResult = cleanup();
        if (hostFailure || cleanupResult == 2) {
            std::cerr << "[INFO] SA01 controlled peer-view oracle INCONCLUSIVE - native host window could not be created\n";
            return 2;
        }
        std::cerr << "[FAIL] SA01 controlled peer-view oracle - plugin fixture refused two native views\n";
        return 0;
    }
    pumpMessages(30);
    for (auto& peer : peers)
        ValidateRect(peer.hwnd, nullptr); // clear previous update regions BEFORE mutation
    constexpr ParamID kReferenceGain = 0x125A0100;
    const ParamValue before = controller->getParamNormalized(kReferenceGain);
    const ParamValue stimulus = before < 0.5 ? 0.75 : 0.25;
    const bool changed = controller->setParamNormalized(kReferenceGain, stimulus) == kResultTrue &&
                         std::fabs(controller->getParamNormalized(kReferenceGain) - stimulus) <= 1.0e-7;
    const bool updatedA = GetUpdateRect(peers[0].hwnd, nullptr, FALSE) != FALSE;
    const bool updatedB = GetUpdateRect(peers[1].hwnd, nullptr, FALSE) != FALSE;
    std::cout << "[INFO] SA01 peer-view observed: parameter " << kReferenceGain
              << " changed=" << changed << ", A invalidated=" << updatedA
              << ", B invalidated=" << updatedB << "\n";
    const bool restored = controller->setParamNormalized(kReferenceGain, before) == kResultTrue;
    const int clean = cleanup();
    if (clean == 2) {
        std::cerr << "[INFO] SA01 INCONCLUSIVE - Win32 host cleanup failed\n";
        return 2;
    }
    if (!changed || !updatedA || !updatedB || !restored || clean != 1) {
        std::cerr << "[FAIL] SA01 controlled peer-view notification - expected two native "
                     "HWND invalidations after accepted edit, observed A=" << updatedA
                  << " B=" << updatedB << " cleanup=" << clean
                  << " restore=" << restored << "\n";
        return 0;
    }
    std::cout << "[PASS] SA01 controlled peer-view notification - both attached HWNDs "
                 "were invalidated by one controller edit; actual rendered UI content NOT TESTED\n";
    return true;
}

class StageReport {
public:
    explicit StageReport(const fs::path& pluginPath)
    : out_(ReportPaths::editorLifecycle(pluginPath), std::ios::binary | std::ios::trunc) {
        if (!out_) { failed_ = true; return; }
        out_ << "125A Plugin Tester / Editor Lifecycle Probe\n"
             << "Version: 0.3.1\n"
             << "Plugin: " << pluginPath.string() << "\n\n";
        out_.flush();
        if (!out_) failed_ = true;
    }

    void mark(const std::string& line) { append("Stage: ", line); }
    void result(const std::string& line) { append("Result: ", line); }

    bool good() const { return !failed_; }
    bool finish() {
        if (out_.is_open()) {
            out_.flush();
            if (!out_) failed_ = true;
            out_.close();
            if (out_.fail()) failed_ = true;
        }
        return !failed_;
    }

private:
    void append(const char* prefix, const std::string& line) {
        if (failed_) return;
        out_ << prefix << line << "\n";
        out_.flush();
        if (!out_) failed_ = true;
    }
    std::ofstream out_;
    bool failed_ = false;
};

int runEditorLifecycleCore(const fs::path& pluginPath, StageReport& stage) {
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
    DWORD registerError = ERROR_SUCCESS;
    if (!ensureWindowClass(registerError)) {
        stage.result("INCONCLUSIVE - native host window class registration failure");
        std::cerr << "[INFO] Editor host infrastructure - RegisterClassExW failed, Win32="
                  << registerError << "\n";
        return 2;
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
                const DWORD windowError = GetLastError();
                view->release();
                controller.close();
                component->terminate();
                std::cerr << "[INFO] Editor host infrastructure - CreateWindowExW failed on cycle "
                          << cycle << " (Win32=" << windowError << ")\n";
                return 2;
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

        stage.mark(std::string("class ") + classInfo.name().data() + " - SA01 dual-view notification oracle");
        const int peerResult = runControlledPeerViewOracle(controller.controller, classInfo.name().data());
        if (peerResult != 1) {
            stage.result(peerResult == 2
                ? "INCONCLUSIVE - SA01 native host window resource failure"
                : "FAIL - SA01 peer-view controller notification or cleanup oracle");
            controller.close();
            component->terminate();
            return peerResult == 2 ? 2 : 1;
        }
        const std::string peerClassName = classInfo.name().data();
        if (peerClassName == "125A QA Good Peer Views")
            stage.result("PASS - SA01 peer-view notification: both HWNDs invalidated; actual pixels NOT TESTED");
        else
            stage.result("NOT TESTED - SA01 peer-view notification: no independent generic visual oracle");
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

int runEditorLifecycle(const fs::path& pluginPath) {
    std::error_code reportEc;
    if (!ReportPaths::ensureRoot(reportEc)) return 2;
    StageReport stage(pluginPath);
    if (!stage.good()) {
        std::cerr << "[INFO] Editor evidence INCONCLUSIVE - cannot open report\n";
        return 2;
    }
    const int coreResult = runEditorLifecycleCore(pluginPath, stage);
    if (!stage.finish()) {
        std::cerr << "[INFO] Editor evidence INCONCLUSIVE - report flush/close failed\n";
        return 2;
    }
    return coreResult;
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
