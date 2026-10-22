#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fobject.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

#ifndef QA_FIXTURE_KIND
#define QA_FIXTURE_KIND 0
#endif

namespace {

#if QA_FIXTURE_KIND == 0
const FUID kProcessorUID(0x125A1000, 0x20260912, 0x00000000, 0x00000001);
const FUID kControllerUID(0x125A2000, 0x20260912, 0x00000000, 0x00000011);
constexpr const char* kPluginName = "125A QA Good Control";
#elif QA_FIXTURE_KIND == 1
const FUID kProcessorUID(0x125A1001, 0x20260912, 0x00000000, 0x00000002);
const FUID kControllerUID(0x125A2001, 0x20260912, 0x00000000, 0x00000012);
constexpr const char* kPluginName = "125A QA Bad Lifecycle";
#elif QA_FIXTURE_KIND == 2
const FUID kProcessorUID(0x125A1002, 0x20260912, 0x00000000, 0x00000003);
const FUID kControllerUID(0x125A2002, 0x20260912, 0x00000000, 0x00000013);
constexpr const char* kPluginName = "125A QA Bad NaN";
#elif QA_FIXTURE_KIND == 3
const FUID kProcessorUID(0x125A1003, 0x20260912, 0x00000000, 0x00000004);
const FUID kControllerUID(0x125A2003, 0x20260912, 0x00000000, 0x00000014);
constexpr const char* kPluginName = "125A QA Bad State";
#elif QA_FIXTURE_KIND == 4
const FUID kProcessorUID(0x125A1004, 0x20260912, 0x00000000, 0x00000005);
const FUID kControllerUID(0x125A2004, 0x20260912, 0x00000000, 0x00000015);
constexpr const char* kPluginName = "125A QA Bad IO";
#elif QA_FIXTURE_KIND == 5
const FUID kProcessorUID(0x125A1005, 0x20260919, 0x00000000, 0x00000006);
const FUID kControllerUID(0x125A2005, 0x20260919, 0x00000000, 0x00000016);
constexpr const char* kPluginName = "125A QA Good Editor";
#elif QA_FIXTURE_KIND == 6
const FUID kProcessorUID(0x125A1006, 0x20260919, 0x00000000, 0x00000007);
const FUID kControllerUID(0x125A2006, 0x20260919, 0x00000000, 0x00000017);
constexpr const char* kPluginName = "125A QA Bad Editor";
#elif QA_FIXTURE_KIND == 7
const FUID kProcessorUID(0x125A1007, 0x20261015, 0x00000000, 0x00000008);
const FUID kControllerUID(0x125A2007, 0x20261015, 0x00000000, 0x00000018);
constexpr const char* kPluginName = "125A QA Bad Ignored State";
#elif QA_FIXTURE_KIND == 8
const FUID kProcessorUID(0x125A1008, 0x20261015, 0x00000000, 0x00000009);
const FUID kControllerUID(0x125A2008, 0x20261015, 0x00000000, 0x00000019);
constexpr const char* kPluginName = "125A QA Bad 64 Bit";
#elif QA_FIXTURE_KIND == 9
const FUID kProcessorUID(0x125A1009, 0x20261015, 0x00000000, 0x00000010);
const FUID kControllerUID(0x125A2009, 0x20261015, 0x00000000, 0x00000020);
constexpr const char* kPluginName = "125A QA Functional Gain";
#elif QA_FIXTURE_KIND == 10
const FUID kProcessorUID(0x125A1010, 0x20261015, 0x00000000, 0x00000011);
const FUID kControllerUID(0x125A2010, 0x20261015, 0x00000000, 0x00000021);
constexpr const char* kPluginName = "125A QA Bad Ignored Gain";
#elif QA_FIXTURE_KIND == 11
const FUID kProcessorUID(0x125A1011, 0x20261015, 0x00000000, 0x00000012);
const FUID kControllerUID(0x125A2011, 0x20261015, 0x00000000, 0x00000022);
constexpr const char* kPluginName = "125A QA Bad Concurrent GUI Audio";
#elif QA_FIXTURE_KIND == 12
const FUID kProcessorUID(0x125A1012, 0x20261015, 0x00000000, 0x00000013);
const FUID kControllerUID(0x125A2012, 0x20261015, 0x00000000, 0x00000023);
constexpr const char* kPluginName = "125A QA Good Event Echo";
#elif QA_FIXTURE_KIND == 13
const FUID kProcessorUID(0x125A1013, 0x20261015, 0x00000000, 0x00000014);
const FUID kControllerUID(0x125A2013, 0x20261015, 0x00000000, 0x00000024);
constexpr const char* kPluginName = "125A QA Bad Missing Event";
#elif QA_FIXTURE_KIND == 14
const FUID kProcessorUID(0x125A1014, 0x20261015, 0x00000000, 0x00000015);
const FUID kControllerUID(0x125A2014, 0x20261015, 0x00000000, 0x00000025);
constexpr const char* kPluginName = "125A QA Bad Callback Heap";
#elif QA_FIXTURE_KIND == 15
const FUID kProcessorUID(0x125A1015, 0x20261015, 0x00000000, 0x00000016);
const FUID kControllerUID(0x125A2015, 0x20261015, 0x00000000, 0x00000026);
constexpr const char* kPluginName = "125A QA Bad Callback Lock";
#elif QA_FIXTURE_KIND == 16
const FUID kProcessorUID(0x125A1016, 0x20261022, 0x00000000, 0x00000017);
const FUID kControllerUID(0x125A2016, 0x20261022, 0x00000000, 0x00000027);
constexpr const char* kPluginName = "125A QA Bad Shared Controller";
#elif QA_FIXTURE_KIND == 17
const FUID kProcessorUID(0x125A1017, 0x20261022, 0x00000000, 0x00000018);
const FUID kControllerUID(0x125A2017, 0x20261022, 0x00000000, 0x00000028);
constexpr const char* kPluginName = "125A QA Good Event Partition";
#elif QA_FIXTURE_KIND == 18
const FUID kProcessorUID(0x125A1018, 0x20261022, 0x00000000, 0x00000019);
const FUID kControllerUID(0x125A2018, 0x20261022, 0x00000000, 0x00000029);
constexpr const char* kPluginName = "125A QA Bad Event Partition";
#elif QA_FIXTURE_KIND == 19
const FUID kProcessorUID(0x125A1019, 0x20261022, 0x00000000, 0x00000020);
const FUID kControllerUID(0x125A2019, 0x20261022, 0x00000000, 0x00000030);
constexpr const char* kPluginName = "125A QA Good Peer Views";
#elif QA_FIXTURE_KIND == 20
const FUID kProcessorUID(0x125A1020, 0x20261022, 0x00000000, 0x00000021);
const FUID kControllerUID(0x125A2020, 0x20261022, 0x00000000, 0x00000031);
constexpr const char* kPluginName = "125A QA Bad Peer Views";
#else
#error Unsupported QA_FIXTURE_KIND
#endif

#if QA_FIXTURE_KIND == 5 || QA_FIXTURE_KIND == 6 || QA_FIXTURE_KIND == 11 || QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20

int gEditorRemovedCount = 0;
std::atomic<bool> gEditorAttached{false};
#if QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
// Instrumented test fixture: this registry is accessed on the host control thread.
std::vector<HWND> gPeerWindows;
#endif

class QAFixtureView final : public FObject, public IPlugView, public IPlugViewContentScaleSupport {
public:
    ~QAFixtureView() override {
        if (frame_)
            frame_->release();
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override {
        return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override {
        if (!parent || isPlatformTypeSupported(type) != kResultTrue)
            return kInvalidArgument;
        attached_ = true;
        gEditorAttached.store(true);
#if QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
        parentWindow_ = static_cast<HWND>(parent);
        gPeerWindows.push_back(parentWindow_);
#endif
        return kResultTrue;
    }

    tresult PLUGIN_API removed() override {
        if (!attached_)
            return kResultFalse;
        attached_ = false;
        gEditorAttached.store(false);
#if QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
        gPeerWindows.erase(std::remove(gPeerWindows.begin(), gPeerWindows.end(), parentWindow_), gPeerWindows.end());
        parentWindow_ = nullptr;
#endif

#if QA_FIXTURE_KIND == 6
        ++gEditorRemovedCount;
        if (gEditorRemovedCount == 2) {
            volatile int* crash = nullptr;
            *crash = 125;
        }
#endif
        return kResultTrue;
    }

    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }

    tresult PLUGIN_API getSize(ViewRect* size) override {
        if (!size)
            return kInvalidArgument;
        *size = rect_;
        return kResultTrue;
    }

    tresult PLUGIN_API onSize(ViewRect* newSize) override {
        if (!newSize)
            return kInvalidArgument;
        rect_ = *newSize;
        return kResultTrue;
    }

    tresult PLUGIN_API onFocus(TBool) override { return kResultTrue; }

    tresult PLUGIN_API setFrame(IPlugFrame* frame) override {
        if (frame == frame_)
            return kResultTrue;
        if (frame_)
            frame_->release();
        frame_ = frame;
        if (frame_)
            frame_->addRef();
        return kResultTrue;
    }

    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        if (!std::isfinite(factor) || factor < 0.5f || factor > 3.0f)
            return kResultFalse;
        rect_.right = static_cast<int32>(320.0f * factor);
        rect_.bottom = static_cast<int32>(180.0f * factor);
        if (frame_) {
            ViewRect request = rect_;
            if (frame_->resizeView(this, &request) != kResultTrue)
                return kResultFalse;
        }
        return kResultTrue;
    }

    tresult PLUGIN_API canResize() override { return kResultFalse; }

    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override {
        return rect ? kResultTrue : kInvalidArgument;
    }

    OBJ_METHODS(QAFixtureView, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugView)
        DEF_INTERFACE(IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)

private:
    ViewRect rect_{0, 0, 320, 180};
    IPlugFrame* frame_ = nullptr;
    bool attached_ = false;
#if QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
    HWND parentWindow_ = nullptr;
#endif
};

#endif

constexpr ParamID kQaReferenceGain = 0x125A0100;
constexpr ParamID kQaReferenceBypass = 0x125A0101;

class QAFixtureController final : public EditController {
public:
    static FUnknown* createInstance(void*) {
        return static_cast<IEditController*>(new QAFixtureController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto result = EditController::initialize(context);
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10 || QA_FIXTURE_KIND == 16 || QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
        if (result == kResultOk) {
            parameters.addParameter(STR16("125A Reference Gain"), STR16("x"), 0, 0.5,
                                    ParameterInfo::kCanAutomate, kQaReferenceGain);
            parameters.addParameter(STR16("125A Reference Bypass"), nullptr, 1, 0.0,
                                    ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
                                    kQaReferenceBypass);
        }
#endif
        return result;
    }

#if QA_FIXTURE_KIND == 16
    // Deliberately broken: all controllers leak one shared parameter value.
    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override {
        const tresult result = EditController::setParamNormalized(tag, value);
        if (result == kResultTrue && tag == kQaReferenceGain)
            sharedGain_ = value;
        return result;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID tag) override {
        if (tag == kQaReferenceGain)
            return sharedGain_;
        return EditController::getParamNormalized(tag);
    }
    static ParamValue sharedGain_;
#endif

#if QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
    // Fault fixture 20 updates first attached view only.
    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override {
        const tresult status = EditController::setParamNormalized(tag, value);
        if (status == kResultTrue && tag == kQaReferenceGain) {
            for (HWND peer : gPeerWindows) {
                if (IsWindow(peer))
                    InvalidateRect(peer, nullptr, FALSE);
#if QA_FIXTURE_KIND == 20
                break;
#endif
            }
        }
        return status;
    }
#endif

    tresult PLUGIN_API setComponentState(IBStream* stream) override {
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
        if (!stream) return kInvalidArgument;
        double gain = 0.0;
        uint32 bypass = 0;
        int32 read = 0;
        if (stream->read(&gain, sizeof(gain), &read) != kResultTrue ||
            read != sizeof(gain) || !std::isfinite(gain) || gain < 0.0 || gain > 1.0)
            return kResultFalse;
        if (stream->read(&bypass, sizeof(bypass), &read) != kResultTrue ||
            read != sizeof(bypass) || bypass > 1)
            return kResultFalse;
        if (setParamNormalized(kQaReferenceGain, gain) != kResultTrue ||
            setParamNormalized(kQaReferenceBypass, static_cast<ParamValue>(bypass)) != kResultTrue)
            return kResultFalse;
#else
        (void)stream;
#endif
        return kResultTrue;
    }

    tresult PLUGIN_API setState(IBStream*) override {
        return kResultTrue;
    }

    tresult PLUGIN_API getState(IBStream*) override {
        return kResultTrue;
    }

    IPlugView* PLUGIN_API createView(FIDString name) override {
#if QA_FIXTURE_KIND == 5 || QA_FIXTURE_KIND == 6 || QA_FIXTURE_KIND == 11 || QA_FIXTURE_KIND == 19 || QA_FIXTURE_KIND == 20
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new QAFixtureView();
#else
        (void)name;
#endif
        return nullptr;
    }
};

#if QA_FIXTURE_KIND == 16
ParamValue QAFixtureController::sharedGain_ = 0.5;
#endif

class QAFixtureProcessor final : public AudioEffect {
public:
    QAFixtureProcessor() {
        setControllerClass(kControllerUID);
#if QA_FIXTURE_KIND == 7
        stateToken_ = ++stateInstanceCounter_;
#endif
    }

    static FUnknown* createInstance(void*) {
        return static_cast<IAudioProcessor*>(new QAFixtureProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const tresult result = AudioEffect::initialize(context);
        if (result != kResultOk)
            return result;

        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
#if QA_FIXTURE_KIND == 12 || QA_FIXTURE_KIND == 13 || QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
        addEventInput(STR16("125A QA Event Echo"), 16);
        addEventOutput(STR16("125A QA Event Echo Out"), 16);
#endif
        return kResultOk;
    }

    // Positive control explicitly advertises both audio precisions. Without
    // this, the production QA only exercises its 32-bit branch.
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override {
#if QA_FIXTURE_KIND == 0 || QA_FIXTURE_KIND == 8 || QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
        return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
                   ? kResultTrue : kResultFalse;
#else
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
#endif
    }

    tresult PLUGIN_API getState(IBStream* stream) override {
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
        if (!stream) return kInvalidArgument;
        int32 written = 0;
        const double gain = gainNormalized_;
        if (stream->write(const_cast<double*>(&gain), sizeof(gain), &written) != kResultTrue ||
            written != sizeof(gain)) return kResultFalse;
        const uint32 bypass = bypass_ ? 1u : 0u;
        if (stream->write(const_cast<uint32*>(&bypass), sizeof(bypass), &written) != kResultTrue ||
            written != sizeof(bypass)) return kResultFalse;
#endif
#if QA_FIXTURE_KIND == 7
        if (!stream) return kInvalidArgument;
        constexpr char marker[] = "125A_STATE_PROBE";
        int32 written = 0;
        if (stream->write(const_cast<char*>(marker), static_cast<int32>(sizeof(marker)), &written) != kResultTrue ||
            written != static_cast<int32>(sizeof(marker))) return kResultFalse;
        written = 0;
        if (stream->write(&stateToken_, static_cast<int32>(sizeof(stateToken_)), &written) != kResultTrue ||
            written != static_cast<int32>(sizeof(stateToken_))) return kResultFalse;
#endif
        return kResultTrue;
    }

    tresult PLUGIN_API setState(IBStream* stream) override {
#if QA_FIXTURE_KIND == 3
        (void)stream;
        return kResultFalse;
#elif QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
        if (!stream) return kInvalidArgument;
        double gain = 0.0;
        uint32 bypass = 0;
        int32 read = 0;
        if (stream->read(&gain, sizeof(gain), &read) != kResultTrue || read != sizeof(gain) ||
            !std::isfinite(gain) || gain < 0.0 || gain > 1.0)
            return kResultFalse;
        if (stream->read(&bypass, sizeof(bypass), &read) != kResultTrue || read != sizeof(bypass) ||
            bypass > 1)
            return kResultFalse;
        gainNormalized_ = gain;
        bypass_ = bypass != 0;
        return kResultTrue;
#else
        (void)stream;
        return kResultTrue;
#endif
    }

    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 busIndex, SpeakerArrangement& arr) override {
#if QA_FIXTURE_KIND == 4
        // Deliberately violate the contract used by the isolated I/O probe:
        // an out-of-range bus index must not be reported as a valid arrangement.
        if (busIndex >= getBusCount(kAudio, dir)) {
            arr = SpeakerArr::kStereo;
            return kResultTrue;
        }
#endif
        return AudioEffect::getBusArrangement(dir, busIndex, arr);
    }

    tresult PLUGIN_API setProcessing(TBool state) override {
#if QA_FIXTURE_KIND == 1
        if (state)
            return kResultFalse;
#else
        (void)state;
#endif
        return kResultTrue;
    }

#if QA_FIXTURE_KIND == 7
    static uint32 stateInstanceCounter_;
    uint32 stateToken_ = 0;
#endif
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
    double gainNormalized_ = 0.5;
    bool bypass_ = false;
#endif

#if QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
    // Bounded fixture state, modified exclusively in process(), not globally.
    Sample32 noteLevel_ = 0.0f;
#endif
    tresult PLUGIN_API process(ProcessData& data) override {
#if QA_FIXTURE_KIND == 14
        void* allocation = ::HeapAlloc(::GetProcessHeap(), 0, 128);
        if (allocation) ::HeapFree(::GetProcessHeap(), 0, allocation);
#elif QA_FIXTURE_KIND == 15
        static std::atomic<uint32_t> callbackCount{0};
        if ((callbackCount.fetch_add(1, std::memory_order_relaxed) & 31u) == 31u)
            (void)::WaitForSingleObject(::GetCurrentThread(), 1);
#endif
#if QA_FIXTURE_KIND == 12 || QA_FIXTURE_KIND == 13 || QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
        if (data.inputEvents && data.outputEvents) {
            const int32 total = data.inputEvents->getEventCount();
            for (int32 i = 0; i < total; ++i) {
#if QA_FIXTURE_KIND == 13
                if (i == total - 1) continue; // Deliberate lost event
#endif
                Event event{};
                if (data.inputEvents->getEvent(i, event) != kResultTrue ||
                    data.outputEvents->addEvent(event) != kResultTrue)
                    return kResultFalse;
            }
        }
#endif
#if QA_FIXTURE_KIND == 11
        if (gEditorAttached.load() && data.numSamples > 0)
            return kResultFalse; // Invalid only during concurrent editor/audio usage
#endif
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
        if (data.inputParameterChanges) {
            const int32 queues = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < queues; ++i) {
                IParamValueQueue* queue = data.inputParameterChanges->getParameterData(i);
                if (!queue || queue->getPointCount() <= 0) continue;
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultTrue)
                    continue;
                if (queue->getParameterId() == kQaReferenceBypass)
                    bypass_ = value >= 0.5;
#if QA_FIXTURE_KIND == 9
                if (queue->getParameterId() == kQaReferenceGain)
                    gainNormalized_ = std::min(1.0, std::max(0.0, value));
#endif
            }
        }
#endif
        if (data.numOutputs <= 0 || data.outputs == nullptr)
            return kResultOk;

#if QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
        // Independent audio oracle: emitted sample equals velocity of most
        // recently arrived NoteOn, NoteOff clears it. Simultaneous notes at
        // one sample use input arrival order. BAD fixture ignores a tied
        // event when running the partitioned 128-frame callback.
        Sample32 referenceAudio[512]{};
        int32 nextEvent = 0;
        int32 lastEventOffset = -1;
        for (int32 n = 0; n < data.numSamples && n < 512; ++n) {
            if (data.inputEvents) {
                const int32 total = data.inputEvents->getEventCount();
                while (nextEvent < total) {
                    Event event{};
                    if (data.inputEvents->getEvent(nextEvent, event) != kResultTrue)
                        return kResultFalse;
                    if (event.sampleOffset > n) break;
                    ++nextEvent;
#if QA_FIXTURE_KIND == 18
                    if (data.numSamples == 128 && event.sampleOffset == lastEventOffset)
                        continue; // controlled SA02 defect: tie not resolved by arrival order
#endif
                    lastEventOffset = event.sampleOffset;
                    if (event.type == Event::kNoteOnEvent)
                        noteLevel_ = event.noteOn.velocity;
                    else if (event.type == Event::kNoteOffEvent)
                        noteLevel_ = 0.0f;
                }
            }
            referenceAudio[n] = noteLevel_;
        }
#endif
        for (int32 busIndex = 0; busIndex < data.numOutputs; ++busIndex) {
            auto& bus = data.outputs[busIndex];
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10 || QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
            bus.silenceFlags = 0;
#else
            bus.silenceFlags = bus.numChannels >= 64
                                   ? ~uint64(0)
                                   : (bus.numChannels > 0 ? ((uint64(1) << bus.numChannels) - 1) : 0);
#endif

            for (int32 channel = 0; channel < bus.numChannels; ++channel) {
                if (data.symbolicSampleSize == kSample32) {
                    Sample32* out = bus.channelBuffers32 ? bus.channelBuffers32[channel] : nullptr;
                    if (!out)
                        continue;
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
                    const Sample32* in = (data.inputs && busIndex < data.numInputs &&
                        data.inputs[busIndex].channelBuffers32 &&
                        channel < data.inputs[busIndex].numChannels)
                        ? data.inputs[busIndex].channelBuffers32[channel] : nullptr;
                    const Sample32 gain = static_cast<Sample32>(bypass_ ? 1.0 : 2.0 * gainNormalized_);
                    for (int32 n = 0; n < data.numSamples; ++n)
                        out[n] = (in ? in[n] : Sample32(0)) * gain;
#elif QA_FIXTURE_KIND == 17 || QA_FIXTURE_KIND == 18
                    for (int32 n = 0; n < data.numSamples && n < 512; ++n)
                        out[n] = referenceAudio[n];
#else
                    if (data.numSamples > 0)
                        std::fill_n(out, data.numSamples, Sample32(0));
#endif
#if QA_FIXTURE_KIND == 2
                    if (data.numSamples > 0) {
                        out[0] = std::numeric_limits<Sample32>::quiet_NaN();
                        bus.silenceFlags = 0;
                    }
#endif
                } else if (data.symbolicSampleSize == kSample64) {
                    Sample64* out = bus.channelBuffers64 ? bus.channelBuffers64[channel] : nullptr;
                    if (!out)
                        continue;
#if QA_FIXTURE_KIND == 9 || QA_FIXTURE_KIND == 10
                    const Sample64* in = (data.inputs && busIndex < data.numInputs &&
                        data.inputs[busIndex].channelBuffers64 &&
                        channel < data.inputs[busIndex].numChannels)
                        ? data.inputs[busIndex].channelBuffers64[channel] : nullptr;
                    const Sample64 gain = static_cast<Sample64>(bypass_ ? 1.0 : 2.0 * gainNormalized_);
                    for (int32 n = 0; n < data.numSamples; ++n)
                        out[n] = (in ? in[n] : Sample64(0)) * gain;
#else
                    if (data.numSamples > 0)
                        std::fill_n(out, data.numSamples, Sample64(0));
#endif
#if QA_FIXTURE_KIND == 2 || QA_FIXTURE_KIND == 8
                    if (data.numSamples > 0) {
                        out[0] = std::numeric_limits<Sample64>::quiet_NaN();
                        bus.silenceFlags = 0;
                    }
#endif
                }
            }
        }
        return kResultOk;
    }
};
#if QA_FIXTURE_KIND == 7
uint32 QAFixtureProcessor::stateInstanceCounter_ = 0;
#endif

} // namespace

BEGIN_FACTORY_DEF("125A Internal QA", "https://github.com/challanger2000", "qa@125a.invalid")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           kPluginName,
           Vst::kDistributable,
           "Fx",
           "1.0.0",
           kVstVersionString,
           QAFixtureProcessor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           kPluginName,
           0,
           "",
           "1.0.0",
           kVstVersionString,
           QAFixtureController::createInstance)

END_FACTORY
