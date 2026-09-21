#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "report_paths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

enum class Level { Pass, Warning, Fail, Info };

struct Result {
    Level level;
    std::string test;
    std::string detail;
};

class Reporter {
public:
    void add(Level level, std::string test, std::string detail = {}) {
        results_.push_back({level, std::move(test), std::move(detail)});
        const auto& r = results_.back();
        std::cout << "[" << label(r.level) << "] " << r.test;
        if (!r.detail.empty()) std::cout << " - " << r.detail;
        std::cout << '\n';
    }

    int failCount() const { return count(Level::Fail); }
    int warningCount() const { return count(Level::Warning); }
    int passCount() const { return count(Level::Pass); }

    std::string summary() const {
        std::ostringstream os;
        os << passCount() << " PASS / " << warningCount() << " WARNING / " << failCount() << " FAIL";
        return os.str();
    }

    bool write(const fs::path& path, const fs::path& pluginPath) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << "125A Plugin Tester / Quality Checker\n";
        out << "Version: 0.2.7\n";
        out << "Plugin: " << pluginPath.string() << "\n\n";
        for (const auto& r : results_) {
            out << "[" << label(r.level) << "] " << r.test;
            if (!r.detail.empty()) out << " - " << r.detail;
            out << '\n';
        }
        out << "\nRESULT: " << summary() << '\n';
        out << "RELEASE: " << (failCount() == 0 ? (warningCount() == 0 ? "PASS" : "REVIEW WARNINGS") : "NOT RECOMMENDED") << '\n';
        return true;
    }

private:
    int count(Level l) const {
        return static_cast<int>(std::count_if(results_.begin(), results_.end(), [l](const auto& r) { return r.level == l; }));
    }

    static const char* label(Level l) {
        switch (l) {
            case Level::Pass: return "PASS";
            case Level::Warning: return "WARN";
            case Level::Fail: return "FAIL";
            case Level::Info: return "INFO";
        }
        return "INFO";
    }

    std::vector<Result> results_;
};

std::string toUtf8(const TChar* text) {
    if (!text) return {};
    return StringConvert::convert(text);
}

std::string busTypeName(MediaType mediaType) {
    return mediaType == kAudio ? "Audio" : mediaType == kEvent ? "Event" : "Unknown";
}

std::string directionName(BusDirection d) {
    return d == kInput ? "Input" : "Output";
}

bool isNormalized(double v) {
    return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}

void inspectBuses(IComponent* component, Reporter& report) {
    if (!component) return;

    for (MediaType mediaType : {kAudio, kEvent}) {
        for (BusDirection direction : {kInput, kOutput}) {
            const int32 count = component->getBusCount(mediaType, direction);
            std::ostringstream detail;
            detail << busTypeName(mediaType) << ' ' << directionName(direction) << " buses: " << count;
            report.add(Level::Info, "Bus count", detail.str());

            for (int32 i = 0; i < count; ++i) {
                BusInfo info{};
                if (component->getBusInfo(mediaType, direction, i, info) != kResultTrue) {
                    report.add(Level::Fail, "Bus metadata", busTypeName(mediaType) + " " + directionName(direction) + " bus #" + std::to_string(i));
                    continue;
                }
                std::ostringstream os;
                os << busTypeName(mediaType) << ' ' << directionName(direction) << " #" << i
                   << ", channels=" << info.channelCount
                   << ", defaultActive=" << ((info.flags & BusInfo::kDefaultActive) ? "yes" : "no");
                report.add(Level::Pass, "Bus metadata", os.str());
            }
        }
    }
}

void inspectParameters(IEditController* controller, Reporter& report) {
    if (!controller) {
        report.add(Level::Warning, "Parameter scan", "No edit controller available");
        return;
    }

    const int32 count = controller->getParameterCount();
    report.add(Level::Info, "Parameter count", std::to_string(count));
    if (count == 0) {
        report.add(Level::Info, "Parameter metadata", "No parameters exported by controller");
        return;
    }

    std::set<ParamID> ids;
    int problematic = 0;
    int bypassCount = 0;

    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info{};
        if (controller->getParameterInfo(i, info) != kResultTrue) {
            report.add(Level::Fail, "Parameter metadata", "Index " + std::to_string(i) + " could not be queried");
            ++problematic;
            continue;
        }

        const auto title = toUtf8(info.title);
        if (!ids.insert(info.id).second) {
            report.add(Level::Fail, "Unique parameter IDs", "Duplicate ID " + std::to_string(info.id));
            ++problematic;
        }
        if (title.empty()) {
            report.add(Level::Warning, "Parameter title", "Parameter ID " + std::to_string(info.id) + " has no title");
            ++problematic;
        }
        if (!isNormalized(info.defaultNormalizedValue)) {
            report.add(Level::Fail, "Parameter default range", "ID " + std::to_string(info.id) + " default=" + std::to_string(info.defaultNormalizedValue));
            ++problematic;
        }
        if (info.stepCount < 0) {
            report.add(Level::Fail, "Parameter step count", "ID " + std::to_string(info.id));
            ++problematic;
        }

        const auto current = controller->getParamNormalized(info.id);
        if (!isNormalized(current)) {
            report.add(Level::Fail, "Parameter current range", "ID " + std::to_string(info.id) + " current=" + std::to_string(current));
            ++problematic;
        }

        if ((info.flags & ParameterInfo::kIsBypass) != 0)
            ++bypassCount;
    }

    if (problematic == 0)
        report.add(Level::Pass, "Parameter metadata", "All parameters have valid IDs/defaults/current values");
    else
        report.add(Level::Warning, "Parameter metadata summary", std::to_string(problematic) + " issue(s) detected");

    if (bypassCount > 1)
        report.add(Level::Fail, "Bypass parameter metadata", std::to_string(bypassCount) + " bypass parameters exported; VST3 allows only one");
    else
        report.add(Level::Info, "Bypass parameter metadata", bypassCount == 1 ? "One bypass parameter exported" : "No bypass parameter exported");

    const std::array<ParamValue, 5> probes {0.0, 0.25, 0.5, 0.75, 1.0};
    int conversionFailures = 0;
    int conversionsChecked = 0;

    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info{};
        if (controller->getParameterInfo(i, info) != kResultTrue)
            continue;

        for (const auto normalized : probes) {
            const ParamValue plain = controller->normalizedParamToPlain(info.id, normalized);
            const ParamValue roundtrip = controller->plainParamToNormalized(info.id, plain);
            ++conversionsChecked;
            if (!std::isfinite(plain) || !isNormalized(roundtrip)) {
                if (conversionFailures < 10) {
                    std::ostringstream detail;
                    detail << "ID " << info.id << ", normalized=" << normalized
                           << ", plain=" << plain << ", back=" << roundtrip;
                    report.add(Level::Fail, "Parameter conversion", detail.str());
                }
                ++conversionFailures;
            }
        }
    }

    if (conversionFailures == 0)
        report.add(Level::Pass, "Parameter conversion stress", std::to_string(conversionsChecked) + " normalized/plain conversions returned finite in-range values");
    else
        report.add(Level::Warning, "Parameter conversion stress summary", std::to_string(conversionFailures) + " invalid conversion(s) across " + std::to_string(conversionsChecked) + " checks");
}

bool findBypassParameter(IEditController* controller, ParamID& bypassId) {
    if (!controller) return false;
    const int32 count = controller->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info{};
        if (controller->getParameterInfo(i, info) == kResultTrue && (info.flags & ParameterInfo::kIsBypass) != 0) {
            bypassId = info.id;
            return true;
        }
    }
    return false;
}

void inspectState(IComponent* component, IEditController* controller, Reporter& report) {
    if (!component) return;

    MemoryStream componentState;
    if (component->getState(&componentState) == kResultTrue) {
        report.add(Level::Info, "Component state size", std::to_string(componentState.getSize()) + " bytes");
        componentState.seek(0, IBStream::kIBSeekSet, nullptr);
        if (component->setState(&componentState) == kResultTrue)
            report.add(Level::Pass, "Component state roundtrip", "getState/setState succeeded");
        else
            report.add(Level::Fail, "Component state roundtrip", "setState rejected the state returned by getState");

        if (controller) {
            componentState.seek(0, IBStream::kIBSeekSet, nullptr);
            if (controller->setComponentState(&componentState) == kResultTrue)
                report.add(Level::Pass, "Controller component-state sync", "setComponentState accepted component state");
            else
                report.add(Level::Warning, "Controller component-state sync", "setComponentState did not accept component state");
        }
    } else {
        report.add(Level::Warning, "Component state save", "getState did not return kResultTrue");
    }

    if (!controller) return;

    MemoryStream controllerState;
    if (controller->getState(&controllerState) == kResultTrue) {
        report.add(Level::Info, "Controller state size", std::to_string(controllerState.getSize()) + " bytes");
        controllerState.seek(0, IBStream::kIBSeekSet, nullptr);
        if (controller->setState(&controllerState) == kResultTrue)
            report.add(Level::Pass, "Controller state roundtrip", "getState/setState succeeded");
        else
            report.add(Level::Fail, "Controller state roundtrip", "setState rejected the state returned by getState");
    } else {
        report.add(Level::Info, "Controller state", "No controller-specific state returned");
    }
}

void inspectProcessingSetups(IAudioProcessor* processor, Reporter& report) {
    if (!processor) return;

    const bool supports32 = processor->canProcessSampleSize(kSample32) == kResultTrue;
    const bool supports64 = processor->canProcessSampleSize(kSample64) == kResultTrue;
    report.add(supports32 ? Level::Pass : Level::Info, "32-bit sample processing", supports32 ? "Supported" : "Not reported as supported");
    report.add(supports64 ? Level::Pass : Level::Info, "64-bit sample processing", supports64 ? "Supported" : "Not reported as supported");

    if (!supports32 && !supports64) {
        report.add(Level::Fail, "Processing setup", "Neither 32-bit nor 64-bit sample processing is supported");
        return;
    }

    const SymbolicSampleSizes sampleSize = supports32 ? kSample32 : kSample64;
    const std::array<double, 5> sampleRates {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    const std::array<int32, 10> blockSizes {1, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int totalAccepted = 0;
    int totalRejected = 0;

    for (const auto sampleRate : sampleRates) {
        int acceptedAtRate = 0;
        std::vector<int32> rejectedBlocks;
        for (const auto blockSize : blockSizes) {
            ProcessSetup setup{};
            setup.processMode = kRealtime;
            setup.symbolicSampleSize = sampleSize;
            setup.maxSamplesPerBlock = blockSize;
            setup.sampleRate = sampleRate;
            if (processor->setupProcessing(setup) == kResultTrue) {
                ++acceptedAtRate;
                ++totalAccepted;
            } else {
                rejectedBlocks.push_back(blockSize);
                ++totalRejected;
            }
        }

        std::ostringstream detail;
        detail << static_cast<int>(sampleRate) << " Hz: " << acceptedAtRate << '/' << blockSizes.size() << " block sizes accepted";
        if (!rejectedBlocks.empty()) {
            detail << " (rejected:";
            for (const auto block : rejectedBlocks) detail << ' ' << block;
            detail << ')';
        }
        report.add(rejectedBlocks.empty() ? Level::Pass : Level::Warning, "Processing setup matrix", detail.str());
    }

    if (totalAccepted == 0) {
        report.add(Level::Fail, "Processing setup summary", "No sample-rate/buffer-size combination was accepted");
        return;
    }

    std::ostringstream summary;
    summary << totalAccepted << '/' << (totalAccepted + totalRejected) << " sample-rate/buffer-size combinations accepted";
    report.add(totalRejected == 0 ? Level::Pass : Level::Warning, "Processing setup summary", summary.str());

    ProcessSetup restore{};
    restore.processMode = kRealtime;
    restore.symbolicSampleSize = sampleSize;
    restore.maxSamplesPerBlock = 512;
    restore.sampleRate = 48000.0;
    if (processor->setupProcessing(restore) == kResultTrue)
        report.add(Level::Pass, "Processing setup restore", "48 kHz / 512 samples accepted");
    else
        report.add(Level::Warning, "Processing setup restore", "48 kHz / 512 samples was not accepted");
}

enum class AudioPattern { Silence, Impulse, DC, Denormal };

const char* audioPatternName(AudioPattern pattern) {
    switch (pattern) {
        case AudioPattern::Silence: return "Silence";
        case AudioPattern::Impulse: return "Impulse";
        case AudioPattern::DC: return "DC";
        case AudioPattern::Denormal: return "Denormal";
    }
    return "Unknown";
}

template <typename Sample>
struct TestBusStorage {
    bool active = false;
    std::vector<std::vector<Sample>> channels;
    std::vector<Sample*> channelPointers;
    AudioBusBuffers bus{};
};

template <typename Sample>
std::vector<TestBusStorage<Sample>> makeAudioBuses(IComponent* component, BusDirection direction, int32 numSamples) {
    std::vector<TestBusStorage<Sample>> result;
    const int32 busCount = component->getBusCount(kAudio, direction);
    result.reserve(static_cast<size_t>(std::max<int32>(0, busCount)));

    for (int32 busIndex = 0; busIndex < busCount; ++busIndex) {
        BusInfo info{};
        TestBusStorage<Sample> storage;

        if (component->getBusInfo(kAudio, direction, busIndex, info) == kResultTrue) {
            storage.active = (info.flags & BusInfo::kDefaultActive) != 0;
            storage.bus.numChannels = std::max<int32>(0, info.channelCount);
            storage.bus.silenceFlags = 0;

            if (storage.active && storage.bus.numChannels > 0) {
                storage.channels.resize(static_cast<size_t>(storage.bus.numChannels));
                storage.channelPointers.resize(storage.channels.size());
                for (size_t channel = 0; channel < storage.channels.size(); ++channel) {
                    storage.channels[channel].assign(static_cast<size_t>(numSamples), static_cast<Sample>(0));
                    storage.channelPointers[channel] = storage.channels[channel].data();
                }
            }
        }

        result.push_back(std::move(storage));
    }

    for (auto& storage : result) {
        if constexpr (sizeof(Sample) == sizeof(Sample32))
            storage.bus.channelBuffers32 = storage.channelPointers.empty() ? nullptr : reinterpret_cast<Sample32**>(storage.channelPointers.data());
        else
            storage.bus.channelBuffers64 = storage.channelPointers.empty() ? nullptr : reinterpret_cast<Sample64**>(storage.channelPointers.data());
    }
    return result;
}

template <typename Sample>
bool hasActiveBuffers(const std::vector<TestBusStorage<Sample>>& buses) {
    return std::any_of(buses.begin(), buses.end(), [](const auto& bus) {
        return bus.active && !bus.channels.empty();
    });
}

template <typename Sample>
void fillInputPattern(std::vector<TestBusStorage<Sample>>& buses, AudioPattern pattern) {
    for (auto& bus : buses) {
        if (!bus.active || bus.channels.empty()) {
            bus.bus.silenceFlags = 0;
            continue;
        }

        bus.bus.silenceFlags = 0;
        for (auto& channel : bus.channels) {
            std::fill(channel.begin(), channel.end(), static_cast<Sample>(0));
            if (pattern == AudioPattern::Impulse && !channel.empty())
                channel[0] = static_cast<Sample>(1.0);
            else if (pattern == AudioPattern::DC)
                std::fill(channel.begin(), channel.end(), static_cast<Sample>(0.5));
            else if (pattern == AudioPattern::Denormal)
                std::fill(channel.begin(), channel.end(), sizeof(Sample) == sizeof(Sample32) ? static_cast<Sample>(1.0e-39) : static_cast<Sample>(1.0e-310));
        }
        if (pattern == AudioPattern::Silence) {
            const int32 channels = std::min<int32>(bus.bus.numChannels, 64);
            bus.bus.silenceFlags = channels == 64 ? ~uint64(0) : (channels > 0 ? ((uint64(1) << channels) - 1) : 0);
        }
    }
}

template <typename Sample>
void clearOutputBuses(std::vector<TestBusStorage<Sample>>& buses) {
    for (auto& bus : buses) {
        bus.bus.silenceFlags = 0;
        for (auto& channel : bus.channels)
            std::fill(channel.begin(), channel.end(), static_cast<Sample>(0));
    }
}

template <typename Sample>
std::vector<AudioBusBuffers> copyBusDescriptors(const std::vector<TestBusStorage<Sample>>& storage) {
    std::vector<AudioBusBuffers> result;
    result.reserve(storage.size());
    for (const auto& bus : storage) result.push_back(bus.bus);
    return result;
}

template <typename Sample>
uint64 countNonFinite(const std::vector<TestBusStorage<Sample>>& buses) {
    uint64 count = 0;
    for (const auto& bus : buses)
        for (const auto& channel : bus.channels)
            for (const auto sample : channel)
                if (!std::isfinite(sample)) ++count;
    return count;
}

template <typename Sample>
void runAudioPattern(IAudioProcessor* processor,
                     SymbolicSampleSizes sampleSize,
                     std::vector<TestBusStorage<Sample>>& inputStorage,
                     std::vector<TestBusStorage<Sample>>& outputStorage,
                     AudioPattern pattern,
                     Reporter& report) {
    constexpr int32 numSamples = 512;
    fillInputPattern(inputStorage, pattern);
    clearOutputBuses(outputStorage);
    auto inputs = copyBusDescriptors(inputStorage);
    auto outputs = copyBusDescriptors(outputStorage);

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = sampleSize;
    data.numSamples = numSamples;
    data.numInputs = static_cast<int32>(inputs.size());
    data.numOutputs = static_cast<int32>(outputs.size());
    data.inputs = inputs.empty() ? nullptr : inputs.data();
    data.outputs = outputs.empty() ? nullptr : outputs.data();

    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";
    const std::string testName = std::string("Audio ") + audioPatternName(pattern) + " process (" + sizeLabel + ")";
    if (processor->process(data) != kResultOk) {
        report.add(Level::Fail, testName, "process() returned failure");
        return;
    }

    uint64 finiteCount = 0;
    uint64 nonFiniteCount = 0;
    uint64 subnormalCount = 0;
    long double sumSquares = 0.0;
    long double sum = 0.0;
    double peak = 0.0;

    for (const auto& bus : outputStorage) {
        for (const auto& channel : bus.channels) {
            for (const auto sample : channel) {
                if (!std::isfinite(sample)) {
                    ++nonFiniteCount;
                    continue;
                }
                if (std::fpclassify(sample) == FP_SUBNORMAL) ++subnormalCount;
                const double value = static_cast<double>(sample);
                ++finiteCount;
                peak = std::max(peak, std::abs(value));
                sumSquares += static_cast<long double>(value) * static_cast<long double>(value);
                sum += static_cast<long double>(value);
            }
        }
    }

    if (nonFiniteCount > 0) {
        report.add(Level::Fail, testName, std::to_string(nonFiniteCount) + " non-finite output sample(s) detected");
        return;
    }

    const double rms = finiteCount > 0 ? std::sqrt(static_cast<double>(sumSquares / finiteCount)) : 0.0;
    const double dc = finiteCount > 0 ? static_cast<double>(sum / finiteCount) : 0.0;
    std::ostringstream detail;
    detail << "finite output; peak=" << peak << ", rms=" << rms << ", dc=" << dc;
    if (subnormalCount > 0) detail << ", subnormal=" << subnormalCount;
    report.add(Level::Pass, testName, detail.str());
}

template <typename Sample>
void runSustainedProcessing(IAudioProcessor* processor,
                            SymbolicSampleSizes sampleSize,
                            std::vector<TestBusStorage<Sample>>& inputStorage,
                            std::vector<TestBusStorage<Sample>>& outputStorage,
                            Reporter& report) {
    constexpr int32 numSamples = 512;
    constexpr int blocks = 256;
    fillInputPattern(inputStorage, AudioPattern::Silence);
    auto inputs = copyBusDescriptors(inputStorage);
    auto outputs = copyBusDescriptors(outputStorage);
    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

    const auto start = std::chrono::steady_clock::now();
    uint64 nonFinite = 0;
    int processed = 0;
    for (int i = 0; i < blocks; ++i) {
        clearOutputBuses(outputStorage);
        for (size_t b = 0; b < outputs.size(); ++b) outputs[b] = outputStorage[b].bus;

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = sampleSize;
        data.numSamples = numSamples;
        data.numInputs = static_cast<int32>(inputs.size());
        data.numOutputs = static_cast<int32>(outputs.size());
        data.inputs = inputs.empty() ? nullptr : inputs.data();
        data.outputs = outputs.empty() ? nullptr : outputs.data();

        if (processor->process(data) != kResultOk) {
            report.add(Level::Fail, "Sustained processing (" + sizeLabel + ")", "process() failed at block " + std::to_string(i));
            return;
        }
        ++processed;
        nonFinite += countNonFinite(outputStorage);
        if (nonFinite > 0) break;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

    if (nonFinite > 0) {
        report.add(Level::Fail, "Sustained processing (" + sizeLabel + ")", std::to_string(nonFinite) + " non-finite output sample(s) detected");
        return;
    }

    std::ostringstream detail;
    detail << processed << " blocks / " << (processed * numSamples) << " samples stable; "
           << elapsed << " us total; " << (processed > 0 ? elapsed / processed : 0) << " us/block";
    report.add(Level::Pass, "Sustained processing (" + sizeLabel + ")", detail.str());
}

template <typename Sample>
void runAutomationStress(IComponent*,
                         IEditController* controller,
                         IAudioProcessor* processor,
                         SymbolicSampleSizes sampleSize,
                         std::vector<TestBusStorage<Sample>>& inputStorage,
                         std::vector<TestBusStorage<Sample>>& outputStorage,
                         Reporter& report) {
    if (!controller) {
        report.add(Level::Info, "Parameter automation stress", "Skipped because no controller is available");
        return;
    }

    std::vector<ParameterInfo> parameters;
    const int32 parameterCount = controller->getParameterCount();
    for (int32 i = 0; i < parameterCount; ++i) {
        ParameterInfo info{};
        if (controller->getParameterInfo(i, info) != kResultTrue) continue;
        const bool canAutomate = (info.flags & ParameterInfo::kCanAutomate) != 0;
        const bool excluded = (info.flags & (ParameterInfo::kIsReadOnly | ParameterInfo::kIsProgramChange | ParameterInfo::kIsHidden)) != 0;
        if (canAutomate && !excluded) parameters.push_back(info);
    }

    if (parameters.empty()) {
        report.add(Level::Info, "Parameter automation stress", "No externally automatable parameters to exercise");
        return;
    }

    constexpr int32 numSamples = 512;
    constexpr size_t batchSize = 64;
    fillInputPattern(inputStorage, AudioPattern::Silence);
    auto inputs = copyBusDescriptors(inputStorage);
    auto outputs = copyBusDescriptors(outputStorage);
    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

    int blocks = 0;
    int points = 0;
    uint64 nonFinite = 0;
    for (size_t begin = 0; begin < parameters.size(); begin += batchSize) {
        const size_t end = std::min(parameters.size(), begin + batchSize);
        ParameterChanges inputChanges(static_cast<int32>(end - begin));
        ParameterChanges outputChanges(std::max<int32>(16, parameterCount));

        for (size_t p = begin; p < end; ++p) {
            int32 queueIndex = 0;
            auto* queue = inputChanges.addParameterData(parameters[p].id, queueIndex);
            if (!queue) {
                report.add(Level::Fail, "Parameter automation stress", "Could not allocate automation queue for parameter ID " + std::to_string(parameters[p].id));
                return;
            }
            int32 pointIndex = 0;
            if (queue->addPoint(0, 0.0, pointIndex) != kResultTrue ||
                queue->addPoint(255, 0.5, pointIndex) != kResultTrue ||
                queue->addPoint(511, 1.0, pointIndex) != kResultTrue) {
                report.add(Level::Fail, "Parameter automation stress", "Could not add automation points for parameter ID " + std::to_string(parameters[p].id));
                return;
            }
            points += 3;
        }

        clearOutputBuses(outputStorage);
        for (size_t b = 0; b < outputs.size(); ++b) outputs[b] = outputStorage[b].bus;

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = sampleSize;
        data.numSamples = numSamples;
        data.numInputs = static_cast<int32>(inputs.size());
        data.numOutputs = static_cast<int32>(outputs.size());
        data.inputs = inputs.empty() ? nullptr : inputs.data();
        data.outputs = outputs.empty() ? nullptr : outputs.data();
        data.inputParameterChanges = &inputChanges;
        data.outputParameterChanges = &outputChanges;

        if (processor->process(data) != kResultOk) {
            report.add(Level::Fail, "Parameter automation stress (" + sizeLabel + ")", "process() failed in automation batch " + std::to_string(blocks));
            return;
        }
        ++blocks;
        nonFinite += countNonFinite(outputStorage);
        if (nonFinite > 0) {
            report.add(Level::Fail, "Parameter automation stress (" + sizeLabel + ")", std::to_string(nonFinite) + " non-finite output sample(s) detected");
            return;
        }
    }

    std::ostringstream detail;
    detail << parameters.size() << " automatable parameter(s), " << points << " sample-accurate point(s), " << blocks << " process block(s) accepted";
    report.add(Level::Pass, "Parameter automation stress (" + sizeLabel + ")", detail.str());
}

template <typename Sample>
void runBypassStress(IEditController* controller,
                     IAudioProcessor* processor,
                     SymbolicSampleSizes sampleSize,
                     std::vector<TestBusStorage<Sample>>& inputStorage,
                     std::vector<TestBusStorage<Sample>>& outputStorage,
                     Reporter& report) {
    ParamID bypassId = 0;
    if (!findBypassParameter(controller, bypassId)) {
        report.add(Level::Info, "Bypass processing stress", "Not applicable: no bypass parameter exported");
        return;
    }

    constexpr int32 numSamples = 512;
    fillInputPattern(inputStorage, hasActiveBuffers(inputStorage) ? AudioPattern::Impulse : AudioPattern::Silence);
    auto inputs = copyBusDescriptors(inputStorage);
    auto outputs = copyBusDescriptors(outputStorage);
    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

    for (const ParamValue bypassValue : {1.0, 0.0}) {
        ParameterChanges changes(1);
        int32 queueIndex = 0;
        auto* queue = changes.addParameterData(bypassId, queueIndex);
        if (!queue) {
            report.add(Level::Fail, "Bypass processing stress (" + sizeLabel + ")", "Could not allocate bypass automation queue");
            return;
        }
        int32 pointIndex = 0;
        if (queue->addPoint(0, bypassValue, pointIndex) != kResultTrue) {
            report.add(Level::Fail, "Bypass processing stress (" + sizeLabel + ")", "Could not add bypass automation point");
            return;
        }

        clearOutputBuses(outputStorage);
        for (size_t b = 0; b < outputs.size(); ++b) outputs[b] = outputStorage[b].bus;

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = sampleSize;
        data.numSamples = numSamples;
        data.numInputs = static_cast<int32>(inputs.size());
        data.numOutputs = static_cast<int32>(outputs.size());
        data.inputs = inputs.empty() ? nullptr : inputs.data();
        data.outputs = outputs.empty() ? nullptr : outputs.data();
        data.inputParameterChanges = &changes;

        if (processor->process(data) != kResultOk) {
            report.add(Level::Fail, "Bypass processing stress (" + sizeLabel + ")", bypassValue > 0.5 ? "process() failed while enabling bypass" : "process() failed while disabling bypass");
            return;
        }
        const uint64 nonFinite = countNonFinite(outputStorage);
        if (nonFinite > 0) {
            report.add(Level::Fail, "Bypass processing stress (" + sizeLabel + ")", std::to_string(nonFinite) + " non-finite output sample(s) detected");
            return;
        }
    }

    report.add(Level::Pass, "Bypass processing stress (" + sizeLabel + ")", "Bypass ON/OFF parameter changes were accepted during processing with finite output");
}

template <typename Sample>
void runLifecycleStress(IComponent* component,
                        IAudioProcessor* processor,
                        SymbolicSampleSizes sampleSize,
                        Reporter& report) {
    constexpr int32 numSamples = 512;
    constexpr int cycles = 8;

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = sampleSize;
    setup.maxSamplesPerBlock = numSamples;
    setup.sampleRate = 48000.0;
    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

    if (processor->setupProcessing(setup) != kResultTrue) {
        report.add(Level::Fail, "Lifecycle stress (" + sizeLabel + ")", "Realtime setup rejected before repeated lifecycle test");
        return;
    }

    auto inputs = makeAudioBuses<Sample>(component, kInput, numSamples);
    auto outputsStorage = makeAudioBuses<Sample>(component, kOutput, numSamples);
    fillInputPattern(inputs, AudioPattern::Silence);
    auto inputDescriptors = copyBusDescriptors(inputs);
    auto outputDescriptors = copyBusDescriptors(outputsStorage);

    for (int cycle = 0; cycle < cycles; ++cycle) {
        if (component->setActive(true) != kResultTrue) {
            report.add(Level::Fail, "Lifecycle stress (" + sizeLabel + ")", "setActive(true) failed at cycle " + std::to_string(cycle + 1));
            return;
        }
        if (processor->setProcessing(true) != kResultTrue) {
            component->setActive(false);
            report.add(Level::Fail, "Lifecycle stress (" + sizeLabel + ")", "setProcessing(true) failed at cycle " + std::to_string(cycle + 1));
            return;
        }

        clearOutputBuses(outputsStorage);
        for (size_t b = 0; b < outputDescriptors.size(); ++b) outputDescriptors[b] = outputsStorage[b].bus;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = sampleSize;
        data.numSamples = numSamples;
        data.numInputs = static_cast<int32>(inputDescriptors.size());
        data.numOutputs = static_cast<int32>(outputDescriptors.size());
        data.inputs = inputDescriptors.empty() ? nullptr : inputDescriptors.data();
        data.outputs = outputDescriptors.empty() ? nullptr : outputDescriptors.data();

        const bool processOk = processor->process(data) == kResultOk;
        const uint64 nonFinite = countNonFinite(outputsStorage);
        const bool stopOk = processor->setProcessing(false) == kResultTrue;
        const bool deactivateOk = component->setActive(false) == kResultTrue;

        if (!processOk || nonFinite > 0 || !stopOk || !deactivateOk) {
            std::ostringstream detail;
            detail << "cycle " << (cycle + 1) << ": process=" << (processOk ? "ok" : "fail")
                   << ", nonFinite=" << nonFinite
                   << ", stop=" << (stopOk ? "ok" : "fail")
                   << ", deactivate=" << (deactivateOk ? "ok" : "fail");
            report.add(Level::Fail, "Lifecycle stress (" + sizeLabel + ")", detail.str());
            return;
        }
    }

    report.add(Level::Pass, "Lifecycle stress (" + sizeLabel + ")", std::to_string(cycles) + " repeated activate/process/deactivate cycles completed cleanly");
}

template <typename Sample>
void runOfflineProcessing(IComponent* component,
                          IAudioProcessor* processor,
                          SymbolicSampleSizes sampleSize,
                          Reporter& report) {
    constexpr int32 numSamples = 512;
    ProcessSetup setup{};
    setup.processMode = kOffline;
    setup.symbolicSampleSize = sampleSize;
    setup.maxSamplesPerBlock = numSamples;
    setup.sampleRate = 48000.0;
    const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

    if (processor->setupProcessing(setup) != kResultTrue) {
        report.add(Level::Warning, "Offline processing (" + sizeLabel + ")", "48 kHz / 512-sample offline setup was not accepted");
        return;
    }

    auto inputsStorage = makeAudioBuses<Sample>(component, kInput, numSamples);
    auto outputsStorage = makeAudioBuses<Sample>(component, kOutput, numSamples);
    fillInputPattern(inputsStorage, AudioPattern::Silence);
    clearOutputBuses(outputsStorage);
    auto inputs = copyBusDescriptors(inputsStorage);
    auto outputs = copyBusDescriptors(outputsStorage);

    if (component->setActive(true) != kResultTrue) {
        report.add(Level::Fail, "Offline processing (" + sizeLabel + ")", "setActive(true) failed after accepting offline setup");
        return;
    }
    if (processor->setProcessing(true) != kResultTrue) {
        component->setActive(false);
        report.add(Level::Fail, "Offline processing (" + sizeLabel + ")", "setProcessing(true) failed after accepting offline setup");
        return;
    }

    ProcessData data{};
    data.processMode = kOffline;
    data.symbolicSampleSize = sampleSize;
    data.numSamples = numSamples;
    data.numInputs = static_cast<int32>(inputs.size());
    data.numOutputs = static_cast<int32>(outputs.size());
    data.inputs = inputs.empty() ? nullptr : inputs.data();
    data.outputs = outputs.empty() ? nullptr : outputs.data();

    const bool processOk = processor->process(data) == kResultOk;
    const uint64 nonFinite = countNonFinite(outputsStorage);
    const bool stopOk = processor->setProcessing(false) == kResultTrue;
    const bool deactivateOk = component->setActive(false) == kResultTrue;

    if (!processOk || nonFinite > 0 || !stopOk || !deactivateOk) {
        std::ostringstream detail;
        detail << "process=" << (processOk ? "ok" : "fail")
               << ", nonFinite=" << nonFinite
               << ", stop=" << (stopOk ? "ok" : "fail")
               << ", deactivate=" << (deactivateOk ? "ok" : "fail");
        report.add(Level::Fail, "Offline processing (" + sizeLabel + ")", detail.str());
        return;
    }

    report.add(Level::Pass, "Offline processing (" + sizeLabel + ")", "Offline setup and process block completed with finite output");
}

void activateDefaultBuses(IComponent* component, Reporter& report) {
    int activated = 0;
    int rejected = 0;
    for (MediaType mediaType : {kAudio, kEvent}) {
        for (BusDirection direction : {kInput, kOutput}) {
            const int32 busCount = component->getBusCount(mediaType, direction);
            for (int32 busIndex = 0; busIndex < busCount; ++busIndex) {
                BusInfo info{};
                if (component->getBusInfo(mediaType, direction, busIndex, info) != kResultTrue) continue;
                if ((info.flags & BusInfo::kDefaultActive) == 0) continue;
                if (component->activateBus(mediaType, direction, busIndex, true) == kResultTrue) ++activated;
                else ++rejected;
            }
        }
    }

    std::ostringstream detail;
    detail << activated << " default-active bus(es) activated";
    if (rejected > 0) detail << ", " << rejected << " activation request(s) rejected";
    report.add(rejected == 0 ? Level::Pass : Level::Warning, "Default bus activation", detail.str());
}

void inspectAudioProcessing(IComponent* component, IEditController* controller, IAudioProcessor* processor, Reporter& report) {
    if (!component || !processor) return;

    constexpr int32 numSamples = 512;
    const bool supports32 = processor->canProcessSampleSize(kSample32) == kResultTrue;
    const bool supports64 = processor->canProcessSampleSize(kSample64) == kResultTrue;
    const int32 outputBusCount = component->getBusCount(kAudio, kOutput);
    if (outputBusCount <= 0) {
        report.add(Level::Info, "Audio torture test", "Skipped because the component exposes no audio output bus");
        return;
    }

    MemoryStream savedState;
    const bool haveSavedState = component->getState(&savedState) == kResultTrue;
    activateDefaultBuses(component, report);

    const std::array<AudioPattern, 4> patterns {AudioPattern::Silence, AudioPattern::Impulse, AudioPattern::DC, AudioPattern::Denormal};
    bool automationRun = false;
    bool bypassRun = false;

    for (const auto sampleSize : {kSample32, kSample64}) {
        const bool supported = sampleSize == kSample32 ? supports32 : supports64;
        if (!supported) continue;

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = sampleSize;
        setup.maxSamplesPerBlock = numSamples;
        setup.sampleRate = 48000.0;
        const std::string sizeLabel = sampleSize == kSample32 ? "32-bit" : "64-bit";

        if (processor->setupProcessing(setup) != kResultTrue) {
            report.add(Level::Fail, "Audio torture setup (" + sizeLabel + ")", "48 kHz / 512 samples rejected");
            continue;
        }
        if (component->setActive(true) != kResultTrue) {
            report.add(Level::Fail, "Audio torture activation (" + sizeLabel + ")", "Component setActive(true) failed");
            continue;
        }
        if (processor->setProcessing(true) != kResultTrue) {
            report.add(Level::Fail, "Audio torture processing state (" + sizeLabel + ")", "setProcessing(true) failed");
            component->setActive(false);
            continue;
        }

        report.add(Level::Pass, "Audio torture lifecycle (" + sizeLabel + ")", "setupProcessing / setActive / setProcessing succeeded");

        if (sampleSize == kSample32) {
            auto inputs = makeAudioBuses<Sample32>(component, kInput, numSamples);
            auto outputs = makeAudioBuses<Sample32>(component, kOutput, numSamples);
            const bool hasInputs = hasActiveBuffers(inputs);
            for (const auto pattern : patterns) {
                if (!hasInputs && pattern != AudioPattern::Silence) {
                    report.add(Level::Info, std::string("Audio ") + audioPatternName(pattern) + " process (32-bit)", "Not applicable: no active audio input bus");
                    continue;
                }
                runAudioPattern(processor, sampleSize, inputs, outputs, pattern, report);
            }
            runSustainedProcessing(processor, sampleSize, inputs, outputs, report);
            if (!automationRun) {
                runAutomationStress(component, controller, processor, sampleSize, inputs, outputs, report);
                automationRun = true;
            }
            if (!bypassRun) {
                runBypassStress(controller, processor, sampleSize, inputs, outputs, report);
                bypassRun = true;
            }
        } else {
            auto inputs = makeAudioBuses<Sample64>(component, kInput, numSamples);
            auto outputs = makeAudioBuses<Sample64>(component, kOutput, numSamples);
            const bool hasInputs = hasActiveBuffers(inputs);
            for (const auto pattern : patterns) {
                if (!hasInputs && pattern != AudioPattern::Silence) {
                    report.add(Level::Info, std::string("Audio ") + audioPatternName(pattern) + " process (64-bit)", "Not applicable: no active audio input bus");
                    continue;
                }
                runAudioPattern(processor, sampleSize, inputs, outputs, pattern, report);
            }
            runSustainedProcessing(processor, sampleSize, inputs, outputs, report);
            if (!automationRun) {
                runAutomationStress(component, controller, processor, sampleSize, inputs, outputs, report);
                automationRun = true;
            }
            if (!bypassRun) {
                runBypassStress(controller, processor, sampleSize, inputs, outputs, report);
                bypassRun = true;
            }
        }

        if (processor->setProcessing(false) != kResultTrue)
            report.add(Level::Warning, "Audio torture stop (" + sizeLabel + ")", "setProcessing(false) returned failure");
        if (component->setActive(false) != kResultTrue)
            report.add(Level::Warning, "Audio torture deactivate (" + sizeLabel + ")", "setActive(false) returned failure");
    }

    const SymbolicSampleSizes stressSampleSize = supports32 ? kSample32 : kSample64;
    if (supports32)
        runLifecycleStress<Sample32>(component, processor, stressSampleSize, report);
    else if (supports64)
        runLifecycleStress<Sample64>(component, processor, stressSampleSize, report);

    if (supports32)
        runOfflineProcessing<Sample32>(component, processor, kSample32, report);
    else if (supports64)
        runOfflineProcessing<Sample64>(component, processor, kSample64, report);

    if (haveSavedState) {
        savedState.seek(0, IBStream::kIBSeekSet, nullptr);
        const bool componentRestored = component->setState(&savedState) == kResultTrue;
        bool controllerRestored = true;
        if (controller) {
            savedState.seek(0, IBStream::kIBSeekSet, nullptr);
            controllerRestored = controller->setComponentState(&savedState) == kResultTrue;
        }
        report.add(componentRestored && controllerRestored ? Level::Pass : Level::Warning,
                   "Post-stress state restore",
                   componentRestored && controllerRestored ? "Saved component state restored after processing stress" : "One or more state restore calls were rejected");
    } else {
        report.add(Level::Info, "Post-stress state restore", "Not available because component getState() did not provide a state");
    }
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "============================================================\n";
    std::cout << "  125A Plugin Tester / Quality Checker v0.2.7\n";
    std::cout << "============================================================\n\n";

    std::string pathText;
    if (argc >= 2) pathText = argv[1];
    else {
        std::cout << "VST3 path eingeben (z.B. C:\\VST3\\Analysator.vst3):\n> ";
        std::getline(std::cin, pathText);
    }

    if (pathText.size() >= 2 && pathText.front() == '"' && pathText.back() == '"')
        pathText = pathText.substr(1, pathText.size() - 2);

    const fs::path pluginPath = fs::u8path(pathText);
    Reporter report;

    if (pathText.empty()) {
        report.add(Level::Fail, "Input", "No VST3 path supplied");
        return 2;
    }
    if (!fs::exists(pluginPath)) {
        report.add(Level::Fail, "Plugin path", "Path does not exist: " + pluginPath.string());
        return 2;
    }

    std::error_code reportEc;
    if (!ReportPaths::ensureRoot(reportEc)) {
        report.add(Level::Fail, "Report storage", "Could not create per-user report directory: " + ReportPaths::root().string());
        return 2;
    }

    report.add(Level::Pass, "Plugin path", pluginPath.string());

    std::string error;
    auto module = Module::create(pluginPath.u8string(), error);
    if (!module) {
        report.add(Level::Fail, "Module load", error.empty() ? "Unknown loading error" : error);
        const auto reportPath = ReportPaths::qa(pluginPath);
        report.write(reportPath, pluginPath);
        return 1;
    }
    report.add(Level::Pass, "Module load", "VST3 module loaded successfully");

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);
    const auto classes = factory.classInfos();
    const bool factoryEnumerationUnsupported = classes.empty();
    report.add(factoryEnumerationUnsupported ? Level::Warning : Level::Pass,
               "Factory classes",
               factoryEnumerationUnsupported ? "0 class(es) - unsupported or special hosting may be required" : std::to_string(classes.size()) + " class(es)");

    int audioClassCount = 0;
    for (const auto& classInfo : classes) {
        std::ostringstream classDetail;
        classDetail << classInfo.name().data() << " | category=" << classInfo.category().data();
        report.add(Level::Info, "Factory class", classDetail.str());
        const bool isMixFxProcessor = std::string(classInfo.category().data()) == "Audio Mix Processor";
        if (classInfo.category() != kVstAudioEffectClass && !isMixFxProcessor)
            continue;

        ++audioClassCount;
        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            report.add(Level::Fail, "Component creation", classInfo.name().data());
            continue;
        }
        report.add(Level::Pass, "Component creation", classInfo.name().data());

        TUID controllerCID{};
        const bool hasControllerCID = component->getControllerClassId(controllerCID) == kResultTrue;
        if (component->initialize(hostContext) != kResultOk) {
            report.add(Level::Fail, "Component initialize", classInfo.name().data());
            continue;
        }
        report.add(Level::Pass, "Component initialize", classInfo.name().data());
        inspectBuses(component.get(), report);

        IPtr<IEditController> controller;
        IEditController* singleController = nullptr;
        bool controllerInitialized = false;
        bool componentsConnected = false;
        IConnectionPoint* componentCP = nullptr;
        IConnectionPoint* controllerCP = nullptr;

        const bool hasSingleController =
            component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&singleController)) == kResultTrue && singleController != nullptr;

        if (hasSingleController) {
            report.add(Level::Pass, "Controller", "Single-component controller acquired");
            controllerInitialized = true;
        } else if (hasControllerCID) {
            controller = factory.createInstance<IEditController>(VST3::UID(controllerCID));
            if (controller) {
                if (controller->initialize(hostContext) == kResultOk) {
                    controllerInitialized = true;
                    report.add(Level::Pass, "Controller initialize", "Separate controller initialized");
                    const bool componentHasCP = component->queryInterface(IConnectionPoint::iid, reinterpret_cast<void**>(&componentCP)) == kResultTrue && componentCP;
                    const bool controllerHasCP = controller->queryInterface(IConnectionPoint::iid, reinterpret_cast<void**>(&controllerCP)) == kResultTrue && controllerCP;
                    if (componentHasCP && controllerHasCP) {
                        const auto c2e = componentCP->connect(controllerCP);
                        const auto e2c = controllerCP->connect(componentCP);
                        if (c2e == kResultTrue && e2c == kResultTrue) {
                            componentsConnected = true;
                            report.add(Level::Pass, "Component/controller connection", "Bidirectional IConnectionPoint connection established");
                        } else {
                            if (c2e == kResultTrue) componentCP->disconnect(controllerCP);
                            if (e2c == kResultTrue) controllerCP->disconnect(componentCP);
                            report.add(Level::Warning, "Component/controller connection", "IConnectionPoint connect call failed");
                        }
                    } else {
                        report.add(Level::Warning, "Component/controller connection", "One or both parts expose no IConnectionPoint");
                    }
                } else {
                    report.add(Level::Fail, "Controller initialize", "Separate controller failed to initialize");
                }
            } else {
                report.add(Level::Fail, "Controller creation", "Controller class could not be created");
            }
        } else {
            report.add(Level::Warning, "Controller", "Component exposes no controller class ID");
        }

        IEditController* activeController = hasSingleController ? singleController : (controllerInitialized ? controller.get() : nullptr);
        if (activeController) inspectParameters(activeController, report);
        else report.add(Level::Warning, "Parameter scan", "Skipped because controller is not initialized");

        inspectState(component.get(), activeController, report);

        IAudioProcessor* processor = nullptr;
        if (component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&processor)) == kResultTrue && processor) {
            report.add(Level::Pass, "IAudioProcessor", "Audio processor interface available");
            report.add(Level::Info, "Processor latency", std::to_string(processor->getLatencySamples()) + " samples");
            const auto tail = processor->getTailSamples();
            report.add(Level::Info, "Processor tail", tail == kInfiniteTail ? "infinite" : std::to_string(tail) + " samples");
            if (isMixFxProcessor) {
                report.add(Level::Info, "Standard VST3 processing lifecycle",
                           "Not applicable: Audio Mix Processor uses the host-specific Mix FX channel lifecycle");
            } else {
                inspectProcessingSetups(processor, report);
                inspectAudioProcessing(component.get(), activeController, processor, report);
            }
            processor->release();
        } else {
            report.add(Level::Fail, "IAudioProcessor", "Supported audio processor class has no IAudioProcessor interface");
        }

        if (componentsConnected) {
            componentCP->disconnect(controllerCP);
            controllerCP->disconnect(componentCP);
        }
        if (componentCP) componentCP->release();
        if (controllerCP) controllerCP->release();
        if (controller && controllerInitialized) controller->terminate();
        if (singleController) singleController->release();
        component->terminate();
    }

    if (audioClassCount == 0) {
        if (factoryEnumerationUnsupported)
            report.add(Level::Warning, "Supported audio processor class", "Not evaluated because the factory exposed no classes");
        else
            report.add(Level::Fail, "Supported audio processor class", "No supported VST3 audio processor class found");
    } else {
        report.add(Level::Pass, "Supported audio processor class", std::to_string(audioClassCount) + " supported processor class(es)");
    }

    const auto reportPath = ReportPaths::qa(pluginPath);
    if (report.write(reportPath, pluginPath)) report.add(Level::Info, "Report", reportPath.string());
    else report.add(Level::Warning, "Report", "Could not write QA report");

    std::cout << "\n============================================================\n";
    std::cout << "RESULT: " << report.summary() << '\n';
    if (report.failCount() > 0) std::cout << "RELEASE: NOT RECOMMENDED\n";
    else if (report.warningCount() > 0) std::cout << "RELEASE: REVIEW WARNINGS\n";
    else std::cout << "RELEASE: PASS\n";
    std::cout << "============================================================\n";

    return report.failCount() == 0 ? 0 : 1;
}
