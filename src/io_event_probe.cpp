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

        if (storage.descriptor.numChannels > 0) {
            storage.pointers.resize(static_cast<size_t>(storage.descriptor.numChannels), nullptr);
        }
        if (storage.active && storage.descriptor.numChannels > 0) {
            storage.channels.resize(static_cast<size_t>(storage.descriptor.numChannels));
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
    // A multi-output MIDI generator can emit events on five or more buses
    // within ONE 512-frame callback. 64 total events is an artificial host
    // bottleneck that can make healthy instruments reject our test host.
    constexpr int32 kOutputEventCapacity = 4096;
    EventList outputEvents(kOutputEventCapacity);
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

    // Only the deterministic 125A event-echo fixtures promise to return
    // every input event. Normal VST3 plugins may transform or consume events.
    BusInfo eventBusInfo{};
    bool echoContract = component->getBusInfo(kEvent, kInput, eventInput.index,
                                               eventBusInfo) == kResultTrue;
    const std::string echoName = "125A QA Event Echo";
    if (echoContract) {
        for (size_t i = 0; i < echoName.size(); ++i) {
            if (eventBusInfo.name[i] != static_cast<char16>(echoName[i])) {
                echoContract = false;
                break;
            }
        }
        if (echoContract && eventBusInfo.name[echoName.size()] != 0)
            echoContract = false;
    }

    ProcessContext transport{};
    transport.sampleRate = 48000.0;
    transport.state = ProcessContext::kPlaying |
                      ProcessContext::kTempoValid |
                      ProcessContext::kTimeSigValid |
                      ProcessContext::kProjectTimeMusicValid;
    transport.timeSigNumerator = 4;
    transport.timeSigDenominator = 4;
    data.processContext = &transport;
    bool processOk = true;
    bool finite = true;
    bool outputEventsValid = true;
    bool echoComplete = true;
    int totalEmitted = 0;
    std::string firstError;
    int firstErrorBlock = -1;
    constexpr int kTransportBlocks = 6;

    for (int block = 0; block < kTransportBlocks; ++block) {
        transport.tempo = block >= 3 ? 137.0 : 120.0;
        transport.timeSigNumerator = block >= 4 ? 3 : 4;
        transport.projectTimeSamples = block == 5 ? 0 : static_cast<int64>(block) * numSamples;
        transport.projectTimeMusic = block == 5 ? 0.0 :
            static_cast<double>(transport.projectTimeSamples) *
            transport.tempo / (60.0 * transport.sampleRate);
        if (block == 1)
            transport.state &= ~ProcessContext::kPlaying;
        else
            transport.state |= ProcessContext::kPlaying;

        data.inputEvents = block == 0 ? &inputEvents : nullptr;
        if (hasEventOutput) outputEvents.clear();
        const tresult processResult = processor->process(data);
        if (processResult != kResultOk) {
            processOk = false;
            firstErrorBlock = block;
            firstError = "process() returned " + std::to_string(processResult) +
                " at transport block " + std::to_string(block) +
                ", event-input-count=" + std::to_string(data.inputEvents ? data.inputEvents->getEventCount() : 0) +
                ", event-output-count=" + std::to_string(hasEventOutput ? outputEvents.getEventCount() : 0) +
                ", shared-host-event-capacity=" + std::to_string(kOutputEventCapacity);
            break;
        }
        if (!outputsFinite(outputs)) {
            finite = false;
            firstErrorBlock = block;
            firstError = "non-finite audio output at transport block " + std::to_string(block);
            break;
        }
        if (hasEventOutput) {
            const int32 emitted = outputEvents.getEventCount();
            totalEmitted += emitted;
            if (echoContract && block == 0 &&
                emitted != inputEvents.getEventCount()) {
                echoComplete = false;
                if (firstError.empty()) firstError = "controlled echo emitted " +
                    std::to_string(emitted) + " events instead of " +
                    std::to_string(inputEvents.getEventCount()) + " in block 0";
            }
            if (echoContract && block > 0 && emitted != 0) {
                echoComplete = false;
                if (firstError.empty()) firstError = "controlled echo emitted " +
                    std::to_string(emitted) + " unexpected events in block " + std::to_string(block);
            }
            for (int32 i = 0; i < emitted; ++i) {
                Event output{};
                const tresult getEventResult = outputEvents.getEvent(i, output);
                if (getEventResult != kResultTrue ||
                    output.sampleOffset < 0 || output.sampleOffset >= numSamples) {
                    outputEventsValid = false;
                    firstErrorBlock = block;
                    if (firstError.empty()) firstError =
                        "invalid emitted event at transport block " + std::to_string(block) +
                        ", output event #" + std::to_string(i) +
                        ", getEvent()=" + std::to_string(getEventResult) +
                        ", sampleOffset=" + std::to_string(output.sampleOffset) +
                        ", expected range 0..511";
                    break;
                }
                if (echoContract && block == 0) {
                    Event expected{};
                    if (inputEvents.getEvent(i, expected) != kResultTrue ||
                        output.type != expected.type ||
                        output.sampleOffset != expected.sampleOffset ||
                        output.busIndex != expected.busIndex) {
                        echoComplete = false;
                        if (firstError.empty()) firstError =
                            "controlled echo metadata mismatch at output event #" +
                            std::to_string(i) + " in transport block 0";
                    }
                }
            }
        } else if (echoContract) {
            echoComplete = false;
            if (firstError.empty()) firstError =
                "controlled event echo has no output event bus available";
        }
        if (!outputEventsValid) break;
    }
    const tresult stopResult = processor->setProcessing(false);
    const tresult deactivateResult = component->setActive(false);
    const tresult inputBusDeactivation = component->activateBus(kEvent, kInput, eventInput.index, false);
    const bool stopOk = stopResult == kResultTrue;
    const bool deactivateOk = deactivateResult == kResultTrue;
    const bool restored = restoreDefaultActivation(component, buses);
    if (!stopOk && firstError.empty()) firstError =
        "setProcessing(false) rejected at end of event stress; result=" + std::to_string(stopResult);
    if (!deactivateOk && firstError.empty()) firstError =
        "setActive(false) rejected at end of event stress; result=" + std::to_string(deactivateResult);
    if (!restored && firstError.empty()) firstError =
        "default bus activation could not be restored after event stress";

    if (!processOk || !finite || !outputEventsValid || !echoComplete || !stopOk || !deactivateOk || !restored) {
        std::cerr << (echoContract && !echoComplete ? "[FAIL] Event completeness - " : "[FAIL] Event stress - ")
                  << (firstError.empty() ? "unknown failure with incomplete diagnostics" : firstError)
                  << "\n";
        std::cerr << "[INFO] Event stress diagnostics -"
                  << " processOk=" << processOk
                  << " finiteAudio=" << finite
                  << " eventOffsetsValid=" << outputEventsValid
                  << " controlledEchoComplete=" << echoComplete
                  << " setProcessingStop=" << stopOk
                  << " setActiveOff=" << deactivateOk
                  << " restoredBuses=" << restored
                  << " eventInputDeactivationResult=" << inputBusDeactivation
                  << " emittedEvents=" << totalEmitted
                  << " eventOutputBuses=" << std::count_if(buses.begin(), buses.end(), [](const BusSnapshot& b) {
                      return b.mediaType == kEvent && b.direction == kOutput;
                  })
                  << " sinkCapacity=" << kOutputEventCapacity
                  << " firstErrorBlock=" << firstErrorBlock
                  << "\n";
        return false;
    }

    std::cout << "[PASS] Event stress - " << (eventPairs * 2)
              << " sample-accurate NoteOn/NoteOff events accepted across " << channels
              << " channel(s), including block boundaries 0/511; output events="
              << totalEmitted << " across " << kTransportBlocks << " transport blocks\n";
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


struct ControlledEvent {
    int32 absoluteSample;
    int16 pitch;
    int32 noteId;
    float velocity;
    bool noteOn;
};

// An independent oracle for *our named QA fixtures only*. Arbitrary MIDI
// instruments can be random, phase-dependent, non-linear or block-sensitive;
// never declare their audio incorrect using this private fixture contract.
constexpr ControlledEvent kPartitionEvents[] = {
    {0,   60, 100, 0.25f, true},
    {127, 61, 101, 0.40f, true},
    {128, 62, 102, 0.35f, true},
    {128, 63, 103, 0.85f, true}, // same sample, explicit arrival precedence
    {250, 63, 103, 0.00f, false}
};

struct PartitionObservation {
    bool completed = false;
    std::vector<Sample32> pcm;
    struct OutputEvent {
        int32 absoluteSample = -1;
        int32 localOffset = -1;
        int32 blockLength = 0;
        int32 type = -1;
        int32 bus = -1;
        int16 channel = -1;
        int16 pitch = -1;
        int32 noteId = -1;
        float velocity = -1.f;
        double ppqPosition = -1.;
        uint16 flags = 0;
        float tuning = -1000.f;
        int32 noteLength = -1; // Only NoteOn has a length
    };
    std::vector<OutputEvent> events;
};

PartitionObservation renderControlledPartition(PluginFactory& factory,
                                               const ClassInfo& classInfo,
                                               FUnknown* hostContext,
                                               bool split) {
    PartitionObservation observation;
    auto component = factory.createInstance<IComponent>(classInfo.ID());
    if (!component || component->initialize(hostContext) != kResultOk)
        return observation;
    IAudioProcessor* processor = nullptr;
    if (component->queryInterface(IAudioProcessor::iid,
             reinterpret_cast<void**>(&processor)) != kResultTrue || !processor) {
        component->terminate();
        return observation;
    }

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = 48000.;
    setup.maxSamplesPerBlock = 256;
    const bool configured = processor->canProcessSampleSize(kSample32) == kResultTrue &&
                            processor->setupProcessing(setup) == kResultTrue;
    const bool busReady = configured &&
        component->activateBus(kAudio, kInput, 0, true) == kResultTrue &&
        component->activateBus(kAudio, kOutput, 0, true) == kResultTrue &&
        component->activateBus(kEvent, kInput, 0, true) == kResultTrue &&
        component->activateBus(kEvent, kOutput, 0, true) == kResultTrue;
    if (!busReady) {
        processor->release();
        component->terminate();
        return observation;
    }

    Sample32 inputL[256]{}, inputR[256]{}, outputL[256]{}, outputR[256]{};
    Sample32* inputPointers[] = {inputL, inputR};
    Sample32* outputPointers[] = {outputL, outputR};
    AudioBusBuffers inputBus{}, outputBus{};
    inputBus.numChannels = 2;
    inputBus.channelBuffers32 = inputPointers;
    inputBus.silenceFlags = 3;
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outputPointers;

    ProcessContext context{};
    context.sampleRate = 48000.;
    context.tempo = 120.;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
    context.state = ProcessContext::kPlaying |
                    ProcessContext::kTempoValid |
                    ProcessContext::kTimeSigValid |
                    ProcessContext::kProjectTimeMusicValid;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;
    data.processContext = &context;

    bool okay = component->setActive(true) == kResultTrue;
    bool processing = okay && processor->setProcessing(true) == kResultTrue;
    okay = okay && processing;
    if (processing) {
        observation.pcm.reserve(256);
        for (int start = 0; start < 256 && okay; start += split ? 128 : 256) {
            const int32 frames = split ? 128 : 256;
            EventList inputs(16), outputs(16);
            for (const auto& item : kPartitionEvents) {
                if (item.absoluteSample < start || item.absoluteSample >= start + frames)
                    continue;
                Event e{};
                e.busIndex = 0;
                e.sampleOffset = item.absoluteSample - start;
                e.ppqPosition = static_cast<double>(item.absoluteSample) / 24000.;
                e.flags = Event::kIsLive;
                if (item.noteOn) {
                    e.type = Event::kNoteOnEvent;
                    e.noteOn.channel = 0;
                    e.noteOn.pitch = item.pitch;
                    e.noteOn.noteId = item.noteId;
                    e.noteOn.velocity = item.velocity;
                } else {
                    e.type = Event::kNoteOffEvent;
                    e.noteOff.channel = 0;
                    e.noteOff.pitch = item.pitch;
                    e.noteOff.noteId = item.noteId;
                    e.noteOff.velocity = 0.f;
                }
                if (inputs.addEvent(e) != kResultTrue) okay = false;
            }
            std::fill_n(outputL, frames, Sample32(0));
            std::fill_n(outputR, frames, Sample32(0));
            context.projectTimeSamples = start;
            context.projectTimeMusic = static_cast<double>(start) / 24000.;
            data.numSamples = frames;
            data.inputEvents = &inputs;
            data.outputEvents = &outputs;
            if (!okay || processor->process(data) != kResultOk) {
                okay = false;
                break;
            }
            for (int32 n = 0; n < frames; ++n) {
                observation.pcm.push_back(outputL[n]);
                if (!std::isfinite(outputL[n]) || !std::isfinite(outputR[n]) ||
                    std::fabs(outputL[n] - outputR[n]) > 1.0e-6f)
                    okay = false;
            }
            for (int32 i = 0; i < outputs.getEventCount(); ++i) {
                Event emitted{};
                if (outputs.getEvent(i, emitted) != kResultTrue) { okay = false; break; }
                // The fixture contract promises a complete event echo.
                // Preserve every semantically significant field BEFORE
                // collapsing block-relative offsets into absolute samples.
                if (emitted.sampleOffset < 0 || emitted.sampleOffset >= frames ||
                    emitted.busIndex != 0) { okay = false; break; }
                PartitionObservation::OutputEvent observed{};
                observed.absoluteSample = start + emitted.sampleOffset;
                observed.localOffset = emitted.sampleOffset;
                observed.blockLength = frames;
                observed.type = emitted.type;
                observed.bus = emitted.busIndex;
                observed.ppqPosition = emitted.ppqPosition;
                observed.flags = emitted.flags;
                if (emitted.type == Event::kNoteOnEvent) {
                    observed.channel = emitted.noteOn.channel;
                    observed.noteId = emitted.noteOn.noteId;
                    observed.pitch = emitted.noteOn.pitch;
                    observed.velocity = emitted.noteOn.velocity;
                    observed.tuning = emitted.noteOn.tuning;
                    observed.noteLength = emitted.noteOn.length;
                } else if (emitted.type == Event::kNoteOffEvent) {
                    observed.channel = emitted.noteOff.channel;
                    observed.noteId = emitted.noteOff.noteId;
                    observed.pitch = emitted.noteOff.pitch;
                    observed.velocity = emitted.noteOff.velocity;
                    observed.tuning = emitted.noteOff.tuning;
                    observed.noteLength = -1;
                } else {
                    okay = false;
                    break;
                }
                observation.events.push_back(observed);
            }
        }
    }
    if (processing && processor->setProcessing(false) != kResultTrue) okay = false;
    if (component->setActive(false) != kResultTrue) okay = false;
    processor->release();
    if (component->terminate() != kResultTrue) okay = false;
    observation.completed = okay;
    return observation;
}

bool runPartitionOracle(PluginFactory& factory,
                        const ClassInfo& classInfo,
                        FUnknown* hostContext) {
    const std::string name = classInfo.name().data();
    const bool good = name == "125A QA Good Event Partition";
    const bool bad = name == "125A QA Bad Event Partition" ||
                     name == "125A QA Bad Event Metadata" ||
                     name == "125A QA Bad Event Echo Fields";
    if (!good && !bad) {
        std::cout << "[INFO] SA02 block-partition equivalence - NOT TESTED: "
                     "arbitrary plugin MIDI/audio semantics have no independent oracle\n";
        return true;
    }
    const auto one = renderControlledPartition(factory, classInfo, hostContext, false);
    const auto split = renderControlledPartition(factory, classInfo, hostContext, true);
    if (!one.completed || !split.completed) {
        std::cerr << "[FAIL] SA02 controlled partition oracle - fixture setup or legal process lifecycle failed\n";
        return false;
    }
    const auto verify = [](const PartitionObservation& x, const char* arrangement) {
        if (x.pcm.size() != 256 ||
            x.events.size() != std::size(kPartitionEvents))
            return false;
        for (size_t i = 0; i < std::size(kPartitionEvents); ++i) {
            const auto& expected = kPartitionEvents[i];
            const auto& actual = x.events[i];
            const int32 expectedType = expected.noteOn ? Event::kNoteOnEvent : Event::kNoteOffEvent;
            const int32 expectedOffset = actual.blockLength > 0
                ? expected.absoluteSample % actual.blockLength : -1;
            if (actual.blockLength <= 0 || expected.absoluteSample < 0 ||
                actual.localOffset < 0 || actual.localOffset >= actual.blockLength ||
                actual.absoluteSample != expected.absoluteSample ||
                actual.localOffset != expectedOffset || actual.type != expectedType ||
                actual.bus != 0 || actual.channel != 0 ||
                actual.noteId != expected.noteId || actual.pitch != expected.pitch ||
                !std::isfinite(actual.velocity) ||
                std::fabs(actual.velocity - (expected.noteOn ? expected.velocity : 0.0f)) > 1.0e-6f ||
                actual.flags != Event::kIsLive ||
                !std::isfinite(actual.ppqPosition) ||
                std::fabs(actual.ppqPosition - static_cast<double>(expected.absoluteSample) / 24000.0) > 1.0e-10 ||
                !std::isfinite(actual.tuning) ||
                std::fabs(actual.tuning) > 1.0e-6f ||
                actual.noteLength != (expected.noteOn ? 0 : -1)) {
                std::cerr << "[INFO] SA02 MIDI field mismatch " << arrangement
                          << " event=" << i << " sample=" << expected.absoluteSample
                          << " expectedType=" << expectedType << " actualType=" << actual.type
                          << " expectedVelocity=" << expected.velocity
                          << " actualVelocity=" << actual.velocity
                          << " expectedBus=0 actualBus=" << actual.bus
                          << " expectedChannel=0 actualChannel=" << actual.channel
                           << " expectedFlags=" << Event::kIsLive << " actualFlags=" << actual.flags
                           << " actualPpq=" << actual.ppqPosition
                           << " actualTuning=" << actual.tuning
                           << " actualLength=" << actual.noteLength << "\n";
                return false;
            }
        }
        for (size_t sample = 0; sample < 256; ++sample) {
            const float expected = sample < 127 ? 0.25f :
                                   sample == 127 ? 0.40f :
                                   sample < 250 ? 0.85f : 0.0f;
            const float actual = x.pcm[sample];
            if (!std::isfinite(actual) || std::fabs(actual - expected) > 1.0e-6f) {
                std::cerr << "[INFO] SA02 fixture PCM observation " << arrangement
                          << " absolute sample=" << sample << " expected=" << expected
                          << " actual=" << actual << "\n";
                return false;
            }
        }
        return true;
    };
    const bool contiguousOkay = verify(one, "256");
    const bool splitOkay = verify(split, "128+128");
    if (!contiguousOkay || !splitOkay) {
        std::cerr << "[FAIL] SA02 block partition equivalence - independent MIDI/PCM "
                     "oracle detects missing/reordered simultaneous events; 256="
                  << contiguousOkay << " 128+128=" << splitOkay << "\n";
        return false;
    }
    std::cout << "[PASS] SA02 block partition equivalence - two independent fresh-instance "
                 "renders (256 and 128+128) matched 256 non-silent PCM samples/channel "
                 "and all five complete controlled MIDI echo records (type, bus, channel, velocity, pitch, ID, timing, flags, tuning and length), including simultaneous events\n";
    return true;
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
        const bool partitionOk = eventOk && runPartitionOracle(factory, classInfo, hostContext);

        processor->release();
        component->terminate();
        if (!arrangementsOk || !sidechainOk || !eventOk || !partitionOk)
            return 1;
    }

    if (audioClasses == 0) {
        std::cout << "[INFO] I/O and event probe - no AudioEffect class exposed\n";
        return 10;
    }

    std::cout << "[PASS] I/O and event probe completed for " << audioClasses << " AudioEffect class(es)\n";
    return 0;
}
