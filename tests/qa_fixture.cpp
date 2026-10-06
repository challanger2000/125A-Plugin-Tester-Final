#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fobject.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <algorithm>
#include <cstring>
#include <limits>

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
#else
#error Unsupported QA_FIXTURE_KIND
#endif

#if QA_FIXTURE_KIND == 5 || QA_FIXTURE_KIND == 6

int gEditorRemovedCount = 0;

class QAFixtureView final : public FObject, public IPlugView {
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
        return kResultTrue;
    }

    tresult PLUGIN_API removed() override {
        if (!attached_)
            return kResultFalse;
        attached_ = false;

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

    tresult PLUGIN_API canResize() override { return kResultFalse; }

    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override {
        return rect ? kResultTrue : kInvalidArgument;
    }

    OBJ_METHODS(QAFixtureView, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugView)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)

private:
    ViewRect rect_{0, 0, 320, 180};
    IPlugFrame* frame_ = nullptr;
    bool attached_ = false;
};

#endif

class QAFixtureController final : public EditController {
public:
    static FUnknown* createInstance(void*) {
        return static_cast<IEditController*>(new QAFixtureController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        return EditController::initialize(context);
    }

    tresult PLUGIN_API setComponentState(IBStream*) override {
        return kResultTrue;
    }

    tresult PLUGIN_API setState(IBStream*) override {
        return kResultTrue;
    }

    tresult PLUGIN_API getState(IBStream*) override {
        return kResultTrue;
    }

    IPlugView* PLUGIN_API createView(FIDString name) override {
#if QA_FIXTURE_KIND == 5 || QA_FIXTURE_KIND == 6
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new QAFixtureView();
#else
        (void)name;
#endif
        return nullptr;
    }
};

class QAFixtureProcessor final : public AudioEffect {
public:
    QAFixtureProcessor() {
        setControllerClass(kControllerUID);
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
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream*) override {
        return kResultTrue;
    }

    tresult PLUGIN_API setState(IBStream*) override {
#if QA_FIXTURE_KIND == 3
        return kResultFalse;
#else
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

    tresult PLUGIN_API process(ProcessData& data) override {
        if (data.numOutputs <= 0 || data.outputs == nullptr)
            return kResultOk;

        for (int32 busIndex = 0; busIndex < data.numOutputs; ++busIndex) {
            auto& bus = data.outputs[busIndex];
            bus.silenceFlags = bus.numChannels >= 64
                                   ? ~uint64(0)
                                   : (bus.numChannels > 0 ? ((uint64(1) << bus.numChannels) - 1) : 0);

            for (int32 channel = 0; channel < bus.numChannels; ++channel) {
                if (data.symbolicSampleSize == kSample32) {
                    Sample32* out = bus.channelBuffers32 ? bus.channelBuffers32[channel] : nullptr;
                    if (!out)
                        continue;
                    if (data.numSamples > 0)
                        std::fill_n(out, data.numSamples, Sample32(0));
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
                    if (data.numSamples > 0)
                        std::fill_n(out, data.numSamples, Sample64(0));
#if QA_FIXTURE_KIND == 2
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
