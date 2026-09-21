#define wmain legacyLauncherMain
#include "launcher.cpp"
#undef wmain

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

constexpr const wchar_t* kProcessingProbePrefix = L"--processing-state-probe=";
constexpr const wchar_t* kValidatorParserSelfTest = L"--validator-parser-selftest";

fs::path processingDiagnosticPath(const fs::path& pluginPath) {
    return ReportPaths::root() /
           (ReportPaths::baseName(pluginPath) + L"_125A_Processing_State_Diagnostic.txt");
}

struct ValidatorFailure {
    std::string test;
    std::string detail;
};

std::vector<ValidatorFailure> parseValidatorFailures(std::istream& input) {
    std::vector<ValidatorFailure> failures;
    std::string currentTest;
    std::string currentError;
    std::string line;

    while (std::getline(input, line)) {
        line = trimAscii(line);
        if (line.empty())
            continue;

        const bool bracketLine = line.size() >= 2 && line.front() == '[' && line.back() == ']';
        const bool failedMarker = bracketLine && line.find("Failed") != std::string::npos;
        const bool succeededMarker = bracketLine && line.find("Succeeded") != std::string::npos;

        if (bracketLine && !failedMarker && !succeededMarker) {
            currentTest = line.substr(1, line.size() - 2);
            currentError.clear();
            continue;
        }

        constexpr const char* errorPrefix = "ERROR:";
        if (line.rfind(errorPrefix, 0) == 0) {
            currentError = trimAscii(line.substr(std::char_traits<char>::length(errorPrefix)));
            continue;
        }

        if (!failedMarker || currentTest.empty())
            continue;

        ValidatorFailure failure;
        failure.test = currentTest;
        failure.detail = currentError.empty()
            ? "Validator subtest failed without additional diagnostic"
            : currentError;

        const auto duplicate = std::find_if(failures.begin(), failures.end(), [&](const ValidatorFailure& existing) {
            return existing.test == failure.test && existing.detail == failure.detail;
        });
        if (duplicate == failures.end())
            failures.push_back(std::move(failure));

        currentError.clear();
    }

    return failures;
}

bool parseQaCounts(const std::string& line, int& pass, int& warning, int& fail) {
    return std::sscanf(line.c_str(), "RESULT: %d PASS / %d WARNING / %d FAIL", &pass, &warning, &fail) == 3;
}

bool normalizeValidatorFindings(const fs::path& pluginPath) {
    std::ifstream validator(validatorReportPath(pluginPath), std::ios::binary);
    if (!validator)
        return true;

    const std::vector<ValidatorFailure> failures = parseValidatorFailures(validator);
    if (failures.empty())
        return true;

    const fs::path qaPath = qaReportPath(pluginPath);
    std::ifstream qaIn(qaPath, std::ios::binary);
    if (!qaIn)
        return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(qaIn, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    qaIn.close();

    constexpr const char* oldPrefix = "[FAIL] Steinberg VST3 validator - ";
    constexpr const char* newPrefix = "[FAIL] Steinberg VST3 validator / ";
    int removedValidatorFailures = 0;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& value) {
        if (value.rfind(oldPrefix, 0) == 0 || value.rfind(newPrefix, 0) == 0) {
            ++removedValidatorFailures;
            return true;
        }
        return false;
    }), lines.end());

    size_t resultIndex = lines.size();
    int passCount = 0;
    int warningCount = 0;
    int failCount = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("RESULT: ", 0) == 0) {
            resultIndex = i;
            if (!parseQaCounts(lines[i], passCount, warningCount, failCount))
                return false;
            break;
        }
    }
    if (resultIndex == lines.size())
        return false;

    failCount = std::max(0, failCount - removedValidatorFailures) + static_cast<int>(failures.size());

    std::vector<std::string> validatorLines;
    validatorLines.reserve(failures.size() + 1);
    for (const auto& failure : failures)
        validatorLines.push_back(std::string(newPrefix) + failure.test + " - " + failure.detail);
    validatorLines.push_back({});
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(resultIndex), validatorLines.begin(), validatorLines.end());

    resultIndex += validatorLines.size();
    std::ostringstream resultLine;
    resultLine << "RESULT: " << passCount << " PASS / " << warningCount << " WARNING / " << failCount << " FAIL";
    lines[resultIndex] = resultLine.str();

    for (size_t i = resultIndex + 1; i < lines.size(); ++i) {
        if (lines[i].rfind("RELEASE: ", 0) != 0)
            continue;
        lines[i] = std::string("RELEASE: ") +
                   (failCount > 0 ? "NOT RECOMMENDED" : (warningCount > 0 ? "REVIEW WARNINGS" : "PASS"));
        break;
    }

    std::ofstream qaOut(qaPath, std::ios::binary | std::ios::trunc);
    if (!qaOut)
        return false;
    for (const auto& outputLine : lines)
        qaOut << outputLine << '\n';
    qaOut.flush();
    return static_cast<bool>(qaOut);
}

int runValidatorParserSelfTest() {
    std::istringstream sample(
        "[Scan Programs]\n"
        "ERROR: Programlist 000->Program 000: has no name!!!\n"
        "[XXXXXXX Failed]\n"
        "[Valid State Transition 32bits]\n"
        "[XXXXXXX Failed]\n"
        "[Bus Activation]\n"
        "ERROR: Bus activation failed.\n"
        "[XXXXXXX Failed]\n");

    const auto failures = parseValidatorFailures(sample);
    if (failures.size() != 3)
        return 1;
    if (failures[0].test != "Scan Programs" ||
        failures[0].detail != "Programlist 000->Program 000: has no name!!!")
        return 1;
    if (failures[1].test != "Valid State Transition 32bits" ||
        failures[1].detail != "Validator subtest failed without additional diagnostic")
        return 1;
    if (failures[2].test != "Bus Activation" ||
        failures[2].detail != "Bus activation failed.")
        return 1;
    return 0;
}

std::string tresultName(tresult result) {
    if (result == kResultTrue)
        return "kResultTrue/kResultOk";
    if (result == kResultFalse)
        return "kResultFalse";
    if (result == kNotImplemented)
        return "kNotImplemented";
    return "other";
}

std::string tresultDescription(tresult result) {
    std::ostringstream out;
    out << tresultName(result)
        << " [signed=" << static_cast<long long>(result)
        << ", hex=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<std::uint32_t>(result) << ']';
    return out.str();
}

const char* sampleSizeName(SymbolicSampleSizes sampleSize) {
    return sampleSize == kSample32 ? "32-bit" : "64-bit";
}

const char* processModeName(ProcessModes mode) {
    return mode == kOffline ? "offline" : "realtime";
}

void activateDefaultBusesForDiagnostic(IComponent* component) {
    for (MediaType mediaType : {kAudio, kEvent}) {
        for (BusDirection direction : {kInput, kOutput}) {
            const int32 busCount = component->getBusCount(mediaType, direction);
            for (int32 busIndex = 0; busIndex < busCount; ++busIndex) {
                BusInfo info{};
                if (component->getBusInfo(mediaType, direction, busIndex, info) != kResultTrue)
                    continue;
                if ((info.flags & BusInfo::kDefaultActive) != 0)
                    component->activateBus(mediaType, direction, busIndex, true);
            }
        }
    }
}

struct ControllerConnection {
    IPtr<IEditController> controller;
    IConnectionPoint* componentCP = nullptr;
    IConnectionPoint* controllerCP = nullptr;
    bool initialized = false;
    bool connected = false;

    void close() {
        if (connected) {
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
        if (controller && initialized) {
            controller->terminate();
            initialized = false;
        }
        controller = nullptr;
    }

    ~ControllerConnection() { close(); }
};

void initializeControllerLikeWorker(IComponent* component,
                                    const PluginFactory& factory,
                                    FUnknown* hostContext,
                                    const TUID& controllerCID,
                                    bool hasControllerCID,
                                    ControllerConnection& connection) {
    if (!hasControllerCID)
        return;

    IEditController* singleController = nullptr;
    const bool single = component->queryInterface(IEditController::iid,
                                                   reinterpret_cast<void**>(&singleController)) == kResultTrue &&
                        singleController != nullptr;
    if (single) {
        singleController->release();
        return;
    }

    connection.controller = factory.createInstance<IEditController>(VST3::UID(controllerCID));
    if (!connection.controller)
        return;
    if (connection.controller->initialize(hostContext) != kResultOk)
        return;
    connection.initialized = true;

    const bool componentHasCP =
        component->queryInterface(IConnectionPoint::iid,
                                  reinterpret_cast<void**>(&connection.componentCP)) == kResultTrue &&
        connection.componentCP;
    const bool controllerHasCP =
        connection.controller->queryInterface(IConnectionPoint::iid,
                                               reinterpret_cast<void**>(&connection.controllerCP)) == kResultTrue &&
        connection.controllerCP;
    if (!componentHasCP || !controllerHasCP)
        return;

    const tresult c2e = connection.componentCP->connect(connection.controllerCP);
    const tresult e2c = connection.controllerCP->connect(connection.componentCP);
    if (c2e == kResultTrue && e2c == kResultTrue) {
        connection.connected = true;
        return;
    }
    if (c2e == kResultTrue)
        connection.componentCP->disconnect(connection.controllerCP);
    if (e2c == kResultTrue)
        connection.controllerCP->disconnect(connection.componentCP);
}

void measureProcessingState(IComponent* component,
                            IAudioProcessor* processor,
                            ProcessModes mode,
                            SymbolicSampleSizes sampleSize,
                            const std::string& className,
                            std::ofstream& report,
                            std::vector<std::string>& summaries) {
    ProcessSetup setup{};
    setup.processMode = mode;
    setup.symbolicSampleSize = sampleSize;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 48000.0;

    const tresult setupResult = processor->setupProcessing(setup);
    report << "Class: " << className << '\n';
    report << "Mode: " << processModeName(mode) << '\n';
    report << "SampleSize: " << sampleSizeName(sampleSize) << '\n';
    report << "setupProcessing: " << tresultDescription(setupResult) << '\n';
    if (setupResult != kResultTrue) {
        report << "setActive(true): not called because setupProcessing failed\n";
        report << "setProcessing(true): not called because setupProcessing failed\n\n";
        return;
    }

    const tresult activeResult = component->setActive(true);
    report << "setActive(true): " << tresultDescription(activeResult) << '\n';
    if (activeResult != kResultTrue) {
        report << "setProcessing(true): not called because setActive(true) failed\n\n";
        return;
    }

    const tresult startResult = processor->setProcessing(true);
    report << "setProcessing(true): " << tresultDescription(startResult) << '\n';

    std::ostringstream summary;
    summary << className << ' ' << processModeName(mode) << ' ' << sampleSizeName(sampleSize)
            << " setProcessing(true)=" << tresultDescription(startResult);
    summaries.push_back(summary.str());

    if (startResult == kResultTrue) {
        const tresult stopResult = processor->setProcessing(false);
        report << "setProcessing(false): " << tresultDescription(stopResult) << '\n';
    } else {
        report << "setProcessing(false): not called because start was rejected\n";
    }

    const tresult deactivateResult = component->setActive(false);
    report << "setActive(false): " << tresultDescription(deactivateResult) << "\n\n";
}

int runProcessingStateDiagnostic(const fs::path& pluginPath) {
    std::error_code ec;
    if (!ReportPaths::ensureRoot(ec))
        return 2;

    std::ofstream report(processingDiagnosticPath(pluginPath), std::ios::binary | std::ios::trunc);
    if (!report)
        return 2;

    report << "125A Plugin Tester / setProcessing Return Diagnostic\n";
    report << "Version: 0.2.7\n";
    report << "Plugin: " << pluginPath.string() << "\n\n";

    std::string error;
    auto module = Module::create(pluginPath.u8string(), error);
    if (!module) {
        report << "ERROR: module load failed: " << error << '\n';
        return 1;
    }

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);

    std::vector<std::string> summaries;
    int audioClasses = 0;

    for (const auto& classInfo : factory.classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass)
            continue;
        ++audioClasses;

        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            report << "Class: " << classInfo.name().data() << "\nERROR: component creation failed\n\n";
            continue;
        }

        TUID controllerCID{};
        const bool hasControllerCID = component->getControllerClassId(controllerCID) == kResultTrue;
        const tresult initializeResult = component->initialize(hostContext);
        report << "Class initialization: " << classInfo.name().data() << " -> "
               << tresultDescription(initializeResult) << '\n';
        if (initializeResult != kResultOk) {
            report << '\n';
            continue;
        }

        ControllerConnection connection;
        initializeControllerLikeWorker(component.get(), factory, hostContext, controllerCID,
                                       hasControllerCID, connection);
        activateDefaultBusesForDiagnostic(component.get());

        IAudioProcessor* processor = nullptr;
        if (component->queryInterface(IAudioProcessor::iid,
                                      reinterpret_cast<void**>(&processor)) != kResultTrue || !processor) {
            report << "ERROR: IAudioProcessor unavailable\n\n";
            connection.close();
            component->terminate();
            continue;
        }

        const bool supports32 = processor->canProcessSampleSize(kSample32) == kResultTrue;
        const bool supports64 = processor->canProcessSampleSize(kSample64) == kResultTrue;
        report << "canProcessSampleSize(32): " << (supports32 ? "yes" : "no") << '\n';
        report << "canProcessSampleSize(64): " << (supports64 ? "yes" : "no") << "\n\n";

        for (ProcessModes mode : {kRealtime, kOffline}) {
            if (supports32)
                measureProcessingState(component.get(), processor, mode, kSample32,
                                       classInfo.name().data(), report, summaries);
            if (supports64)
                measureProcessingState(component.get(), processor, mode, kSample64,
                                       classInfo.name().data(), report, summaries);
        }

        processor->release();
        connection.close();
        component->terminate();
    }

    report << "AudioEffectClasses: " << audioClasses << '\n';
    for (const auto& summary : summaries)
        report << "SUMMARY: " << summary << '\n';
    report.flush();
    return report ? 0 : 2;
}

bool qaContainsSetProcessingFailure(const fs::path& pluginPath) {
    std::ifstream in(qaReportPath(pluginPath), std::ios::binary);
    if (!in)
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str().find("setProcessing(true) failed") != std::string::npos;
}

std::string processingDiagnosticSummary(const fs::path& pluginPath) {
    std::ifstream in(processingDiagnosticPath(pluginPath), std::ios::binary);
    if (!in)
        return {};

    std::string line;
    std::vector<std::string> summaries;
    while (std::getline(in, line)) {
        constexpr const char* prefix = "SUMMARY: ";
        if (line.rfind(prefix, 0) == 0)
            summaries.push_back(line.substr(std::char_traits<char>::length(prefix)));
    }

    if (summaries.empty())
        return {};

    std::ostringstream result;
    for (size_t i = 0; i < summaries.size(); ++i) {
        if (i > 0)
            result << " | ";
        result << summaries[i];
    }
    return result.str();
}

bool isNormalPluginInvocation(int argc, wchar_t** argv, fs::path& pluginPath) {
    if (argc != 2 || !argv || !argv[1])
        return false;
    const std::wstring argument(argv[1]);
    if (argument.rfind(L"--", 0) == 0)
        return false;
    pluginPath = fs::path(argument);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && argv && argv[1]) {
        const std::wstring argument(argv[1]);
        if (argument == kValidatorParserSelfTest)
            return runValidatorParserSelfTest();

        const size_t prefixLength = std::char_traits<wchar_t>::length(kProcessingProbePrefix);
        if (argument.rfind(kProcessingProbePrefix, 0) == 0) {
            const fs::path pluginPath(argument.substr(prefixLength));
            return runProcessingStateDiagnostic(pluginPath);
        }
    }

    fs::path pluginPath;
    const bool normalPluginInvocation = isNormalPluginInvocation(argc, argv, pluginPath);
    if (normalPluginInvocation) {
        std::error_code ec;
        fs::remove(processingDiagnosticPath(pluginPath), ec);
    }

    const int result = legacyLauncherMain(argc, argv);

    if (normalPluginInvocation && !normalizeValidatorFindings(pluginPath))
        std::wcerr << L"[WARN] Steinberg validator findings could not be normalized into the QA report\n";

    if (!normalPluginInvocation || !qaContainsSetProcessingFailure(pluginPath))
        return result;

    const fs::path self = executablePath();
    if (self.empty())
        return result;

    const std::wstring probeArgument = std::wstring(kProcessingProbePrefix) + pluginPath.wstring();
    const int diagnosticResult = runIsolated(self,
                                             probeArgument,
                                             pluginPath,
                                             180000,
                                             "setProcessing return diagnostic",
                                             false);
    if (diagnosticResult != 0) {
        std::wcerr << L"[WARN] setProcessing diagnostic could not be completed; exit code "
                   << diagnosticResult << L'\n';
        return result;
    }

    const std::string summary = processingDiagnosticSummary(pluginPath);
    if (!summary.empty()) {
        if (!annotateReport(pluginPath,
                            "INFO",
                            "setProcessing return diagnostic",
                            summary,
                            0, 0, 0)) {
            std::wcerr << L"[WARN] setProcessing diagnostic could not be integrated into QA report\n";
        }
    }

    return result;
}
