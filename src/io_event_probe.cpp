#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

struct BusSnapshot {
    MediaType mediaType{};
    BusDirection direction{};
    int32 index{};
    bool defaultActive{};
    bool defaultActivationAccepted{};
    BusType busType{};
    int32 channels{};
};

bool activateAndRestoreBuses(IComponent* component, std::vector<BusSnapshot>& buses) {
    int reversible = 0;
    int fixedOrRejected = 0;
    int auxInputs = 0;

    for (MediaType mediaType : {kAudio, kEvent}) {
        for (BusDirection direction : {kInput, kOutput}) {
            const int32 count = component->getBusCount(mediaType, direction);
            if (count < 0) {
                std::cerr << "[FAIL] Bus count returned a negative value\n";
                return false;
            }

            for (int32 index = 0; index < count; ++index) {
                BusInfo info{};
                if (component->getBusInfo(mediaType, direction, index, info) != kResultTrue) {
                    std::cerr << "[FAIL] getBusInfo failed for bus " << index << "\n";
                    return false;
                }
                if (info.channelCount < 0) {
                    std::cerr << "[FAIL] Bus reports a negative channel count\n";
                    return false;
                }

                const bool defaultActive = (info.flags & BusInfo::kDefaultActive) != 0;
                bool defaultActivationAccepted = false;
                if (defaultActive) {
                    defaultActivationAccepted = component->activateBus(mediaType, direction, index, true) == kResultTrue;
                    if (!defaultActivationAccepted) {
                        ++fixedOrRejected;
                    } else if (component->activateBus(mediaType, direction, index, false) == kResultTrue) {
                        ++reversible;
                        if (component->activateBus(mediaType, direction, index, true) != kResultTrue) {
                            std::cerr << "[FAIL] Bus accepted deactivation but rejected restoration\n";
                            return false;
                        }
                    }
                } else {
                    if (component->activateBus(mediaType, direction, index, true) == kResultTrue) {
                        ++reversible;
                        if (component->activateBus(mediaType, direction, index, false) != kResultTrue) {
                            std::cerr << "[FAIL] Bus accepted activation but rejected restoration\n";
                            return false;
                        }
                    } else {
                        ++fixedOrRejected;
                    }
                }

                buses.push_back({mediaType, direction, index, defaultActive, defaultActivationAccepted, info.busType, info.channelCount});
                if (mediaType == kAudio && direction == kInput && info.busType == kAux)
                    ++auxInputs;
            }
        }
    }

    std::cout << "[PASS] Bus activation/restoration - " << buses.size()
              << " bus(es), " << reversible << " reversible transition(s), "
              << fixedOrRejected << " fixed/rejected transition(s), "
              << auxInputs << " auxiliary audio input(s)\n";
    return true;
}

bool restoreDefaultActivation(IComponent* component, const std::vector<BusSnapshot>& buses) {
    for (const auto& bus : buses) {
        if (bus.defaultActive && bus.defaultActivationAccepted) {
            if (component->activateBus(bus.mediaType, bus.direction, bus.index, true) != kResultTrue)
                return false;
        } else {
            component->activateBus(bus.mediaType, bus.direction, bus.index, false);
        }
    }
    return true;
}

bool inspectBusArrangements(IComponent* component, IAudioProcessor* processor) {
    const int32 inputCount = component->getBusCount(kAudio, kInput);
    const int32 outputCount = component->getBusCount(kAudio, kOutput);
    if (inputCount < 0 || outputCount < 0) {
        std::cerr << "[FAIL] Bus arrangement - negative audio bus count\n";
        return false;
    }

    std::vector<SpeakerArrangement> inputs(static_cast<size_t>(inputCount));
    std::vector<SpeakerArrangement> outputs(static_cast<size_t>(outputCount));

    auto queryDirectionWithMetadata = [&](BusDirection direction, std::vector<SpeakerArrangement>& arrangements) {
        for (int32 index = 0; index < static_cast<int32>(arrangements.size()); ++index) {
            SpeakerArrangement arrangement{};
            if (processor->getBusArrangement(direction, index, arrangement) != kResultTrue) {
                std::cerr << "[FAIL] Bus arrangement - getBusArrangement failed for index " << index << "\n";
                return false;
            }
            BusInfo info{};
            if (component->getBusInfo(kAudio, direction, index, info) != kResultTrue) {
                std::cerr << "[FAIL] Bus arrangement - getBusInfo failed during arrangement check\n";
                return false;
            }
            const int32 arrangementChannels = SpeakerArr::getChannelCount(arrangement);
            if (arrangementChannels != info.channelCount) {
                std::cerr << "[FAIL] Bus arrangement - channel count mismatch at index " << index
                          << " (BusInfo=" << info.channelCount << ", arrangement=" << arrangementChannels << ")\n";
                return false;
            }
            arrangements[static_cast<size_t>(index)] = arrangement;
        }
        return true;
    };

    auto readCurrentArrangements = [&](std::vector<SpeakerArrangement>& currentInputs,
                                       std::vector<SpeakerArrangement>& currentOutputs) {
        currentInputs.assign(static_cast<size_t>(inputCount), SpeakerArrangement{});
        currentOutputs.assign(static_cast<size_t>(outputCount), SpeakerArrangement{});
        for (int32 index = 0; index < inputCount; ++index) {
            if (processor->getBusArrangement(kInput, index, currentInputs[static_cast<size_t>(index)]) != kResultTrue)
                return false;
        }
        for (int32 index = 0; index < outputCount; ++index) {
            if (processor->getBusArrangement(kOutput, index, currentOutputs[static_cast<size_t>(index)]) != kResultTrue)
                return false;
        }
        return true;
    };

    if (!queryDirectionWithMetadata(kInput, inputs) || !queryDirectionWithMetadata(kOutput, outputs))
        return false;

    auto restoreOriginalArrangements = [&]() {
        // VST3 permits setBusArrangements() to return non-success while still adapting the
        // current layout. Therefore the return code alone cannot prove that restoration failed.
        processor->setBusArrangements(inputs.empty() ? nullptr : inputs.data(), inputCount,
                                      outputs.empty() ? nullptr : outputs.data(), outputCount);
        std::vector<SpeakerArrangement> restoredInputs;
        std::vector<SpeakerArrangement> restoredOutputs;
        if (!readCurrentArrangements(restoredInputs, restoredOutputs)) {
            std::cerr << "[FAIL] Bus arrangement - could not query layout after restoration request\n";
            return false;
        }
        if (restoredInputs != inputs || restoredOutputs != outputs) {
            std::cerr << "[FAIL] Bus arrangement - original layout could not be restored after probe\n";
            return false;
        }
        return true;
    };

    SpeakerArrangement dummy{};
    if (processor->getBusArrangement(kInput, inputCount, dummy) == kResultTrue ||
        processor->getBusArrangement(kOutput, outputCount, dummy) == kResultTrue) {
        std::cerr << "[FAIL] Bus arrangement - out-of-range bus index was accepted\n";
        return false;
    }

    const auto setResult = processor->setBusArrangements(inputs.empty() ? nullptr : inputs.data(), inputCount,
                                                          outputs.empty() ? nullptr : outputs.data(), outputCount);
    std::vector<SpeakerArrangement> roundtripInputs;
    std::vector<SpeakerArrangement> roundtripOutputs;
    if (!readCurrentArrangements(roundtripInputs, roundtripOutputs)) {
        std::cerr << "[FAIL] Bus arrangement roundtrip - could not query layout after setBusArrangements\n";
        return false;
    }

    const bool roundtripPreserved = roundtripInputs == inputs && roundtripOutputs == outputs;
    if (!roundtripPreserved && !restoreOriginalArrangements())
        return false;

    if (setResult == kResultTrue) {
        if (!roundtripPreserved) {
            std::cerr << "[FAIL] Bus arrangement roundtrip - plug-in accepted current arrangements but changed them\n";
            return false;
        }
        std::cout << "[PASS] Bus arrangement roundtrip - current layouts accepted and preserved\n";
    } else if (roundtripPreserved) {
        std::cout << "[INFO] Bus arrangement roundtrip - current layout request was rejected without mutating the layout\n";
    } else {
        std::cout << "[INFO] Bus arrangement roundtrip - non-success return adapted the layout; original layout was restored before continuing\n";
    }

    // Controlled alternate-layout probe. Rejection is legitimate. A non-success return may still
    // adapt the layout, so every mutation is detected and restored before later processing tests.
    auto alternateInputs = inputs;
    auto alternateOutputs = outputs;
    bool haveAlternate = false;
    BusDirection changedDirection = kInput;
    int32 changedIndex = -1;
    SpeakerArrangement requested{};

    auto chooseAlternate = [&](BusDirection direction, const std::vector<SpeakerArrangement>& current, std::vector<SpeakerArrangement>& alternate) {
        for (int32 index = 0; index < static_cast<int32>(current.size()); ++index) {
            BusInfo info{};
            if (component->getBusInfo(kAudio, direction, index, info) != kResultTrue || info.busType != kMain)
                continue;
            const int32 channels = SpeakerArr::getChannelCount(current[static_cast<size_t>(index)]);
            if (channels == 1 || channels == 2) {
                requested = channels == 1 ? SpeakerArr::kStereo : SpeakerArr::kMono;
                alternate[static_cast<size_t>(index)] = requested;
                changedDirection = direction;
                changedIndex = index;
                return true;
            }
        }
        return false;
    };

    haveAlternate = chooseAlternate(kInput, inputs, alternateInputs);
    if (!haveAlternate)
        haveAlternate = chooseAlternate(kOutput, outputs, alternateOutputs);

    if (haveAlternate) {
        const auto alternateResult = processor->setBusArrangements(alternateInputs.empty() ? nullptr : alternateInputs.data(), inputCount,
                                                                    alternateOutputs.empty() ? nullptr : alternateOutputs.data(), outputCount);
        std::vector<SpeakerArrangement> actualInputs;
        std::vector<SpeakerArrangement> actualOutputs;
        if (!readCurrentArrangements(actualInputs, actualOutputs)) {
            std::cerr << "[FAIL] Bus arrangement alternate - could not query layout after alternate request\n";
            return false;
        }

        const bool matchesRequested = actualInputs == alternateInputs && actualOutputs == alternateOutputs;
        const bool layoutChanged = actualInputs != inputs || actualOutputs != outputs;

        if (layoutChanged && !restoreOriginalArrangements())
            return false;

        if (alternateResult == kResultTrue) {
            if (!matchesRequested) {
                std::cerr << "[FAIL] Bus arrangement alternate - accepted request was not reflected by getBusArrangement\n";
                return false;
            }
            std::cout << "[PASS] Bus arrangement alternate - accepted mono/stereo change was truthful and reversible\n";
        } else if (layoutChanged) {
            std::cout << "[INFO] Bus arrangement alternate - non-success return adapted the layout; original layout was restored\n";
        } else {
            std::cout << "[INFO] Bus arrangement alternate - mono/stereo alternate layout not supported; rejection left layout unchanged\n";
        }
    } else {
        std::cout << "[INFO] Bus arrangement alternate - no suitable mono/stereo main bus\n";
    }

    return true;
}

template <typename Sample>
struct AudioStorage {
    bool active = false;
    std::vector<std::vector<Sample>> channels;
    std::vector<Sample*> pointers;
    AudioBusBuffers descriptor{};
};

template <typename Sample>
std::vector<AudioStorage<Sample>> makeAudioBuses(const std::vector<BusSnapshot>& buses,
                                                  BusDirection direction,
                                                  int32 numSamples,
                                                  int32 forceActiveIndex = -1) {
    int32 maxIndex = -1;
    for (const auto& bus : buses) {
        if (bus.mediaType == kAudio && bus.direction == direction)
            maxIndex = std::max(maxIndex, bus.index);
    }

    std::vector<AudioStorage<Sample>> result(static_cast<size_t>(maxIndex + 1));
    for (const auto& bus : buses) {
        if (bus.mediaType != kAudio || bus.direction != direction)
            continue;

        auto& storage = result[static_cast<size_t>(bus.index)];
        const bool activeByDefault = bus.defaultActive && bus.defaultActivationAccepted;
        storage.active = activeByDefault || bus.index == forceActiveIndex;
        storage.descriptor.numChannels = std::max<int32>(0, bus.channels);
        storage.descriptor.silenceFlags = 0;

        if (storage.active && storage.descriptor.numChannels > 0) {
            storage.channels.resize(static_cast<size_t>(storage.descriptor.numChannels));
            storage.pointers.resize(storage.channels.size());
            for (size_t channel = 0; channel < storage.channels.size(); ++channel) {
                storage.channels[channel].assign(static_cast<size_t>(numSamples), static_cast<Sample>(0));
                storage.pointers[channel] = storage.channels[channel].data();
            }
            const int32 silenceChannels = std::min<int32>(storage.descriptor.numChannels, 64);
            storage.descriptor.silenceFlags = silenceChannels == 64
                                                  ? ~uint64(0)
                                                  : (silenceChannels > 0 ? ((uint64(1) << silenceChannels) - 1) : 0);
        }
    }

    for (auto& storage : result) {
        if constexpr (sizeof(Sample) == sizeof(Sample32))
            storage.descriptor.channelBuffers32 = storage.pointers.empty() ? nullptr : reinterpret_cast<Sample32**>(storage.pointers.data());
        else
            storage.descriptor.channelBuffers64 = storage.pointers.empty() ? nullptr : reinterpret_cast<Sample64**>(storage.pointers.data());
    }
    return result;
}

template <typename Sample>
std::vector<AudioBusBuffers> descriptors(const std::vector<AudioStorage<Sample>>& storage) {
    std::vector<AudioBusBuffers> result;
    result.reserve(storage.size());
    for (const auto& bus : storage)
        result.push_back(bus.descriptor);
    return result;
}

template <typename Sample>
bool outputsFinite(const std::vector<AudioStorage<Sample>>& buses) {
    for (const auto& bus : buses)
        for (const auto& channel : bus.channels)
            for (const auto sample : channel)
                if (!std::isfinite(sample))
                    return false;
    return true;
}

bool configureProcessing(IAudioProcessor* processor, SymbolicSampleSizes& sampleSize) {
    const bool supports32 = processor->canProcessSampleSize(kSample32) == kResultTrue;
    const bool supports64 = processor->canProcessSampleSize(kSample64) == kResultTrue;
    if (!supports32 && !supports64)
        return false;

    sampleSize = supports32 ? kSample32 : kSample64;
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = sampleSize;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 48000.0;
    return processor->setupProcessing(setup) == kResultTrue;
}

template <typename Sample>
bool runOneSidechain(IComponent* component,
                     IAudioProcessor* processor,
                     const std::vector<BusSnapshot>& buses,
                     int32 auxIndex,
                     SymbolicSampleSizes sampleSize) {
    constexpr int32 numSamples = 512;
    if (!restoreDefaultActivation(component, buses)) {
        std::cerr << "[FAIL] Sidechain stress - default restoration failed\n";
        return false;
    }
    if (component->activateBus(kAudio, kInput, auxIndex, true) != kResultTrue) {
        std::cout << "[INFO] Sidechain stress - auxiliary input #" << auxIndex << " activation rejected\n";
        return true;
    }

    auto inputs = makeAudioBuses<Sample>(buses, kInput, numSamples, auxIndex);
    auto outputs = makeAudioBuses<Sample>(buses, kOutput, numSamples);
    if (static_cast<size_t>(auxIndex) < inputs.size()) {
        auto& aux = inputs[static_cast<size_t>(auxIndex)];
        aux.descriptor.silenceFlags = 0;
        for (auto& channel : aux.channels)
            if (!channel.empty()) channel[0] = static_cast<Sample>(0.5);
    }
    auto inputDescriptors = descriptors(inputs);
    auto outputDescriptors = descriptors(outputs);

    if (component->setActive(true) != kResultTrue) {
        component->activateBus(kAudio, kInput, auxIndex, false);
        std::cerr << "[FAIL] Sidechain stress - setActive(true) failed\n";
        return false;
    }
    if (processor->setProcessing(true) != kResultTrue) {
        component->setActive(false);
        component->activateBus(kAudio, kInput, auxIndex, false);
        std::cerr << "[FAIL] Sidechain stress - setProcessing(true) failed\n";
        return false;
    }

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = sampleSize;
    data.numSamples = numSamples;
    data.numInputs = static_cast<int32>(inputDescriptors.size());
    data.numOutputs = static_cast<int32>(outputDescriptors.size());
    data.inputs = inputDescriptors.empty() ? nullptr : inputDescriptors.data();
    data.outputs = outputDescriptors.empty() ? nullptr : outputDescriptors.data();

    const bool processOk = processor->process(data) == kResultOk;
    const bool finite = outputsFinite(outputs);
    const bool stopOk = processor->setProcessing(false) == kResultTrue;
    const bool deactivateOk = component->setActive(false) == kResultTrue;
    component->activateBus(kAudio, kInput, auxIndex, false);
    const bool restored = restoreDefaultActivation(component, buses);

    if (!processOk || !finite || !stopOk || !deactivateOk || !restored) {
        std::cerr << "[FAIL] Sidechain stress - lifecycle/processing failed for auxiliary input #" << auxIndex << "\n";
        return false;
    }

    std::cout << "[PASS] Sidechain stress - auxiliary audio input #" << auxIndex << " processed finite audio\n";
    return true;
}

bool runSidechainStress(IComponent* component, IAudioProcessor* processor, const std::vector<BusSnapshot>& buses) {
    std::vector<int32> auxIndices;
    for (const auto& bus : buses) {
        if (bus.mediaType == kAudio && bus.direction == kInput && bus.busType == kAux && bus.channels > 0)
            auxIndices.push_back(bus.index);
    }
    if (auxIndices.empty()) {
        std::cout << "[INFO] Sidechain stress - Not applicable: no auxiliary audio input bus\n";
        return true;
    }

    SymbolicSampleSizes sampleSize{};
    if (!configureProcessing(processor, sampleSize)) {
        std::cerr << "[FAIL] Sidechain stress - no usable 48 kHz / 512 processing setup\n";
        return false;
    }

    for (const auto auxIndex : auxIndices) {
        const bool ok = sampleSize == kSample32
                            ? runOneSidechain<Sample32>(component, processor, buses, auxIndex, sampleSize)
                            : runOneSidechain<Sample64>(component, processor, buses, auxIndex, sampleSize);
        if (!ok)
            return false;
    }
    return true;
}

template <typename Sample>
bool runEventStressTyped(IComponent* component,
                         IAudioProcessor* processor,
                         const std::vector<BusSnapshot>& buses,
                         const BusSnapshot& eventInput,
                         SymbolicSampleSizes sampleSize) {
    constexpr int32 numSamples = 512;
    if (!restoreDefaultActivation(component, buses)) {
        std::cerr << "[FAIL] Event stress - default restoration failed\n";
        return false;
    }
    if (component->activateBus(kEvent, kInput, eventInput.index, true) != kResultTrue) {
        std::cout << "[INFO] Event stress - event input exists but activation was rejected\n";
        return true;
    }

    auto inputs = makeAudioBuses<Sample>(buses, kInput, numSamples);
    auto outputs = makeAudioBuses<Sample>(buses, kOutput, numSamples);
    auto inputDescriptors = descriptors(inputs);
    auto outputDescriptors = descriptors(outputs);

    if (component->setActive(true) != kResultTrue) {
        component->activateBus(kEvent, kInput, eventInput.index, false);
        std::cerr << "[FAIL] Event stress - setActive(true) failed\n";
        return false;
    }
    if (processor->setProcessing(true) != kResultTrue) {
        component->setActive(false);
        component->activateBus(kEvent, kInput, eventInput.index, false);
        std::cerr << "[FAIL] Event stress - setProcessing(true) failed\n";
        return false;
    }

    EventList inputEvents(64);
    EventList outputEvents(64);
    const int16 channels = static_cast<int16>(std::max<int32>(1, std::min<int32>(eventInput.channels, 16)));
    constexpr int eventPairs = 8;

    for (int i = 0; i < eventPairs; ++i) {
        const int32 onOffset = i == 0 ? 0 : i * 56;
        const int32 offOffset = i == eventPairs - 1 ? 511 : std::min<int32>(511, onOffset + 32);
        const int16 channel = static_cast<int16>(i % channels);
        const int16 pitch = static_cast<int16>(48 + i);
        const int32 noteId = 1250 + i;

        Event noteOn{};
        noteOn.busIndex = eventInput.index;
        noteOn.sampleOffset = onOffset;
        noteOn.ppqPosition = 0.0;
        noteOn.flags = Event::kIsLive;
        noteOn.type = Event::kNoteOnEvent;
        noteOn.noteOn.channel = channel;
        noteOn.noteOn.pitch = pitch;
        noteOn.noteOn.tuning = 0.f;
        noteOn.noteOn.velocity = static_cast<float>(0.35 + 0.07 * i);
        noteOn.noteOn.length = offOffset - onOffset + 1;
        noteOn.noteOn.noteId = noteId;

        Event noteOff{};
        noteOff.busIndex = eventInput.index;
        noteOff.sampleOffset = offOffset;
        noteOff.ppqPosition = 0.0;
        noteOff.flags = Event::kIsLive;
        noteOff.type = Event::kNoteOffEvent;
        noteOff.noteOff.channel = channel;
        noteOff.noteOff.pitch = pitch;
        noteOff.noteOff.velocity = 0.f;
        noteOff.noteOff.noteId = noteId;
        noteOff.noteOff.tuning = 0.f;

        if (inputEvents.addEvent(noteOn) != kResultTrue || inputEvents.addEvent(noteOff) != kResultTrue) {
            processor->setProcessing(false);
            component->setActive(false);
            component->activateBus(kEvent, kInput, eventInput.index, false);
            std::cerr << "[FAIL] Event stress - could not construct dense event list\n";
            return false;
        }
    }

    const bool hasEventOutput = std::any_of(buses.begin(), buses.end(), [](const BusSnapshot& b) {
        return b.mediaType == kEvent && b.direction == kOutput && b.channels > 0;
    });

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = sampleSize;
    data.numSamples = numSamples;
    data.numInputs = static_cast<int32>(inputDescriptors.size());
    data.numOutputs = static_cast<int32>(outputDescriptors.size());
    data.inputs = inputDescriptors.empty() ? nullptr : inputDescriptors.data();
    data.outputs = outputDescriptors.empty() ? nullptr : outputDescriptors.data();
    data.inputEvents = &inputEvents;
    data.outputEvents = hasEventOutput ? &outputEvents : nullptr;

    const bool processOk = processor->process(data) == kResultOk;
    const bool finite = outputsFinite(outputs);

    bool outputEventsValid = true;
    if (hasEventOutput) {
        for (int32 i = 0; i < outputEvents.getEventCount(); ++i) {
            Event event{};
            if (outputEvents.getEvent(i, event) != kResultTrue || event.sampleOffset < 0 || event.sampleOffset >= numSamples) {
                outputEventsValid = false;
                break;
            }
        }
    }

    const bool stopOk = processor->setProcessing(false) == kResultTrue;
    const bool deactivateOk = component->setActive(false) == kResultTrue;
    component->activateBus(kEvent, kInput, eventInput.index, false);
    const bool restored = restoreDefaultActivation(component, buses);

    if (!processOk || !finite || !outputEventsValid || !stopOk || !deactivateOk || !restored) {
        std::cerr << "[FAIL] Event stress - dense event processing/lifecycle or output-event validation failed\n";
        return false;
    }

    std::cout << "[PASS] Event stress - " << (eventPairs * 2)
              << " sample-accurate NoteOn/NoteOff events accepted across " << channels
              << " channel(s), including block boundaries 0/511; output events="
              << (hasEventOutput ? outputEvents.getEventCount() : 0) << "\n";
    return true;
}

bool runEventStress(IComponent* component, IAudioProcessor* processor, const std::vector<BusSnapshot>& buses) {
    const auto eventInput = std::find_if(buses.begin(), buses.end(), [](const BusSnapshot& b) {
        return b.mediaType == kEvent && b.direction == kInput && b.channels > 0;
    });
    if (eventInput == buses.end()) {
        std::cout << "[INFO] Event stress - Not applicable: no event input bus\n";
        return true;
    }

    SymbolicSampleSizes sampleSize{};
    if (!configureProcessing(processor, sampleSize)) {
        std::cerr << "[FAIL] Event stress - no usable 48 kHz / 512 processing setup\n";
        return false;
    }

    return sampleSize == kSample32
               ? runEventStressTyped<Sample32>(component, processor, buses, *eventInput, sampleSize)
               : runEventStressTyped<Sample64>(component, processor, buses, *eventInput, sampleSize);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: 125A_Plugin_Tester_IOEventProbe.exe <plugin.vst3>\n";
        return 2;
    }

    std::string error;
    auto module = Module::create(argv[1], error);
    if (!module) {
        std::cerr << "[FAIL] Module load - " << error << "\n";
        return 1;
    }

    HostApplication host;
    FUnknown* hostContext = &host;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);

    int audioClasses = 0;
    for (const auto& classInfo : factory.classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass && std::string(classInfo.category().data()) != "Audio Mix Processor")
            continue;
        ++audioClasses;

        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            std::cerr << "[FAIL] Component creation\n";
            return 1;
        }
        if (component->initialize(hostContext) != kResultOk) {
            std::cerr << "[FAIL] Component initialize\n";
            return 1;
        }

        std::vector<BusSnapshot> buses;
        if (!activateAndRestoreBuses(component.get(), buses)) {
            component->terminate();
            return 1;
        }

        IAudioProcessor* processor = nullptr;
        if (component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&processor)) != kResultTrue || !processor) {
            component->terminate();
            std::cerr << "[FAIL] IAudioProcessor unavailable\n";
            return 1;
        }

        const bool arrangementsOk = inspectBusArrangements(component.get(), processor);
        const bool sidechainOk = arrangementsOk && runSidechainStress(component.get(), processor, buses);
        const bool eventOk = sidechainOk && runEventStress(component.get(), processor, buses);

        processor->release();
        component->terminate();
        if (!arrangementsOk || !sidechainOk || !eventOk)
            return 1;
    }

    if (audioClasses == 0) {
        std::cout << "[INFO] I/O and event probe - no AudioEffect class exposed\n";
        return 10;
    }

    std::cout << "[PASS] I/O and event probe completed for " << audioClasses << " AudioEffect class(es)\n";
    return 0;
}
