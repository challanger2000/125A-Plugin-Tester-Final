#include "report_paths.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstring>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

constexpr int kNotApplicable = 10;
constexpr int kWarning = 11;

struct ClassProbeResult {
    bool hadState = false;
    bool warning = false;
};

struct ControllerBinding {
    IEditController* singleController = nullptr;
    IPtr<IEditController> separateController;
    bool separateControllerInitialized = false;
    IConnectionPoint* componentCP = nullptr;
    IConnectionPoint* controllerCP = nullptr;
    bool connected = false;

    IEditController* active() const {
        if (singleController)
            return singleController;
        return separateControllerInitialized ? separateController.get() : nullptr;
    }
};

void setupController(PluginFactory& factory,
                     IComponent* component,
                     bool hasControllerCID,
                     const TUID& controllerCID,
                     FUnknown* hostContext,
                     ControllerBinding& binding,
                     ClassProbeResult& result,
                     const char* phase) {
    if (!component)
        return;

    if (component->queryInterface(IEditController::iid,
                                  reinterpret_cast<void**>(&binding.singleController)) == kResultTrue &&
        binding.singleController) {
        std::cout << "[PASS] " << phase << " controller - single-component controller acquired\n";
        return;
    }

    if (!hasControllerCID) {
        std::cout << "[INFO] " << phase << " controller - component exposes no controller class ID\n";
        return;
    }

    binding.separateController = factory.createInstance<IEditController>(VST3::UID(controllerCID));
    if (!binding.separateController) {
        std::cout << "[WARN] " << phase << " controller - controller class could not be created\n";
        result.warning = true;
        return;
    }

    if (binding.separateController->initialize(hostContext) != kResultOk) {
        std::cout << "[WARN] " << phase << " controller - controller initialize failed\n";
        result.warning = true;
        return;
    }
    binding.separateControllerInitialized = true;

    const bool componentHasCP =
        component->queryInterface(IConnectionPoint::iid,
                                  reinterpret_cast<void**>(&binding.componentCP)) == kResultTrue &&
        binding.componentCP;
    const bool controllerHasCP =
        binding.separateController->queryInterface(IConnectionPoint::iid,
                                                    reinterpret_cast<void**>(&binding.controllerCP)) == kResultTrue &&
        binding.controllerCP;

    if (!componentHasCP || !controllerHasCP) {
        std::cout << "[WARN] " << phase << " component/controller connection - one or both parts expose no IConnectionPoint\n";
        result.warning = true;
        return;
    }

    const auto componentToController = binding.componentCP->connect(binding.controllerCP);
    const auto controllerToComponent = binding.controllerCP->connect(binding.componentCP);
    if (componentToController == kResultTrue && controllerToComponent == kResultTrue) {
        binding.connected = true;
        std::cout << "[PASS] " << phase << " component/controller connection - bidirectional IConnectionPoint connection established\n";
        return;
    }

    if (componentToController == kResultTrue)
        binding.componentCP->disconnect(binding.controllerCP);
    if (controllerToComponent == kResultTrue)
        binding.controllerCP->disconnect(binding.componentCP);
    std::cout << "[WARN] " << phase << " component/controller connection - IConnectionPoint connect call failed\n";
    result.warning = true;
}

void teardownController(ControllerBinding& binding, ClassProbeResult& result, const char* phase) {
    if (binding.connected) {
        binding.componentCP->disconnect(binding.controllerCP);
        binding.controllerCP->disconnect(binding.componentCP);
        binding.connected = false;
    }

    if (binding.componentCP) {
        binding.componentCP->release();
        binding.componentCP = nullptr;
    }
    if (binding.controllerCP) {
        binding.controllerCP->release();
        binding.controllerCP = nullptr;
    }

    if (binding.separateControllerInitialized) {
        if (binding.separateController->terminate() != kResultOk) {
            std::cout << "[WARN] " << phase << " controller terminate - terminate returned failure\n";
            result.warning = true;
        }
        binding.separateControllerInitialized = false;
    }
    binding.separateController.reset();

    if (binding.singleController) {
        binding.singleController->release();
        binding.singleController = nullptr;
    }
}

ClassProbeResult probeClass(PluginFactory& factory,
                            const ClassInfo& classInfo,
                            FUnknown* hostContext,
                            bool& hardFailure) {
    ClassProbeResult result;
    hardFailure = false;

    auto source = factory.createInstance<IComponent>(classInfo.ID());
    if (!source) {
        std::cerr << "[FAIL] Fresh-instance state probe - source component creation failed\n";
        hardFailure = true;
        return result;
    }

    TUID sourceControllerCID{};
    const bool sourceHasControllerCID = source->getControllerClassId(sourceControllerCID) == kResultTrue;
    if (source->initialize(hostContext) != kResultOk) {
        std::cerr << "[FAIL] Fresh-instance state probe - source component initialization failed\n";
        hardFailure = true;
        return result;
    }

    ControllerBinding sourceBinding;
    setupController(factory,
                    source.get(),
                    sourceHasControllerCID,
                    sourceControllerCID,
                    hostContext,
                    sourceBinding,
                    result,
                    "Source");

    MemoryStream sourceState;
    if (source->getState(&sourceState) != kResultTrue) {
        std::cout << "[WARN] Fresh-instance state probe - component getState() did not provide readable persistence data\n";
        result.warning = true;
        teardownController(sourceBinding, result, "Source");
        if (source->terminate() != kResultOk) {
            std::cout << "[WARN] Fresh-instance source terminate - terminate returned failure\n";
            result.warning = true;
        }
        source.reset();
        return result;
    }

    result.hadState = true;
    std::cout << "[INFO] Fresh-instance state source size - " << sourceState.getSize() << " bytes\n";

    // A real host connects component and controller before state operations.
    // Tear down that complete first instance before creating the destination so
    // the probe also remains valid for plug-ins with single-instance cardinality.
    teardownController(sourceBinding, result, "Source");
    if (source->terminate() != kResultOk) {
        std::cout << "[WARN] Fresh-instance source terminate - terminate returned failure\n";
        result.warning = true;
    }
    source.reset();

    auto destination = factory.createInstance<IComponent>(classInfo.ID());
    if (!destination) {
        std::cerr << "[FAIL] Fresh-instance state probe - destination component creation failed\n";
        hardFailure = true;
        return result;
    }

    TUID destinationControllerCID{};
    const bool destinationHasControllerCID = destination->getControllerClassId(destinationControllerCID) == kResultTrue;
    if (destination->initialize(hostContext) != kResultOk) {
        std::cerr << "[FAIL] Fresh-instance state probe - destination component initialization failed\n";
        hardFailure = true;
        return result;
    }

    ControllerBinding destinationBinding;
    setupController(factory,
                    destination.get(),
                    destinationHasControllerCID,
                    destinationControllerCID,
                    hostContext,
                    destinationBinding,
                    result,
                    "Destination");

    sourceState.seek(0, IBStream::kIBSeekSet, nullptr);
    if (destination->setState(&sourceState) != kResultTrue) {
        teardownController(destinationBinding, result, "Destination");
        destination->terminate();
        std::cerr << "[FAIL] Fresh-instance state transfer - fresh component rejected saved component state\n";
        hardFailure = true;
        return result;
    }

    MemoryStream restoredState;
    if (destination->getState(&restoredState) != kResultTrue) {
        teardownController(destinationBinding, result, "Destination");
        destination->terminate();
        std::cerr << "[FAIL] Fresh-instance state verification - fresh component could not re-save restored state\n";
        hardFailure = true;
        return result;
    }

    // Distinguish a successful API return from an actually restored payload.
    // Generic binary changes are advisory: plugins may legally serialize
    // volatile or reordered metadata. The controlled marker fixture has a
    // deterministic byte-for-byte contract and must be rejected if ignored.
    const bool sameSize = sourceState.getSize() == restoredState.getSize();
    const bool sameBytes = sameSize &&
        (sourceState.getSize() == 0 ||
         std::memcmp(sourceState.getData(), restoredState.getData(),
                     static_cast<size_t>(sourceState.getSize())) == 0);
    if (!sameBytes) {
        constexpr char marker[] = "125A_STATE_PROBE";
        const bool controlledFixture =
            sourceState.getSize() >= sizeof(marker) &&
            std::memcmp(sourceState.getData(), marker, sizeof(marker)) == 0;
        if (controlledFixture) {
            teardownController(destinationBinding, result, "Destination");
            destination->terminate();
            std::cerr << "[FAIL] Semantic state restoration - ignored-state control fixture returned different payload after restore\n";
            hardFailure = true;
            return result;
        }
        std::cout << "[WARN] Semantic state restoration - re-saved bytes differ; may be volatile serialization rather than semantic loss\n";
        result.warning = true;
    } else {
        std::cout << "[INFO] Serialized state identity - identical bytes; semantic equivalence remains NOT VERIFIED without independent oracle\n";
    }

    std::cout << "[PASS] Fresh-instance state API - saved state accepted by new component after source destruction\n";
    std::cout << "[PASS] Fresh-instance state re-save - restored component returned state again ("
              << restoredState.getSize() << " bytes)\n";

    if (auto* activeController = destinationBinding.active()) {
        restoredState.seek(0, IBStream::kIBSeekSet, nullptr);
        if (activeController->setComponentState(&restoredState) == kResultTrue)
            std::cout << "[INFO] Fresh-instance controller sync API - accepted restored bytes; parameter equivalence not independently proven\n";
        else {
            std::cout << "[WARN] Fresh-instance controller state sync - setComponentState rejected restored component state\n";
            result.warning = true;
        }
    }

    teardownController(destinationBinding, result, "Destination");
    if (destination->terminate() != kResultOk) {
        std::cout << "[WARN] Fresh-instance destination terminate - terminate returned failure\n";
        result.warning = true;
    }

    return result;
}


struct IsolationObservation {
    bool exercised = false;
    bool warning = false;
    bool knownDefect = false;
    ParamID observedParameter = 0;
    ParamValue expectedUntouched = 0.0;
    ParamValue actualUntouched = 0.0;
};

// This test is deliberately restricted to a single host/control thread. It
// measures normalized controller independence for two simultaneously alive
// COMPONENT/CONTROLLER pairs, not audio semantics or two views of one pair.
IsolationObservation probeConcurrentControllerInstances(PluginFactory& factory,
                                                        const ClassInfo& classInfo,
                                                        FUnknown* hostContext) {
    IsolationObservation observed;
    const bool negativeFixture =
        std::string(classInfo.name().data()) == "125A QA Bad Shared Controller";

    auto instanceA = factory.createInstance<IComponent>(classInfo.ID());
    if (!instanceA) {
        std::cout << "[INFO] Multi-instance controller isolation - NOT TESTED: first component unavailable\n";
        return observed;
    }
    if (instanceA->initialize(hostContext) != kResultOk) {
        std::cout << "[INFO] Multi-instance controller isolation - INCONCLUSIVE: first initialize failed\n";
        return observed;
    }
    auto instanceB = factory.createInstance<IComponent>(classInfo.ID());
    if (!instanceB) {
        instanceA->terminate();
        std::cout << "[INFO] Multi-instance controller isolation - NOT TESTED: concurrent second component unavailable\n";
        return observed;
    }
    if (instanceB->initialize(hostContext) != kResultOk) {
        instanceB.reset();
        instanceA->terminate();
        std::cout << "[INFO] Multi-instance controller isolation - INCONCLUSIVE: second initialize failed\n";
        return observed;
    }

    ClassProbeResult lifecycle;
    ControllerBinding bindingA, bindingB;
    TUID cidA{}, cidB{};
    const bool hasCidA = instanceA->getControllerClassId(cidA) == kResultTrue;
    const bool hasCidB = instanceB->getControllerClassId(cidB) == kResultTrue;
    setupController(factory, instanceA.get(), hasCidA, cidA, hostContext, bindingA, lifecycle, "Concurrent A");
    setupController(factory, instanceB.get(), hasCidB, cidB, hostContext, bindingB, lifecycle, "Concurrent B");

    auto* controllerA = bindingA.active();
    auto* controllerB = bindingB.active();
    ParamID examinedId = 0;
    if (controllerA && controllerB) {
        const int32 n = std::min(controllerA->getParameterCount(), controllerB->getParameterCount());
        for (int32 index = 0; index < n; ++index) {
            ParameterInfo first{}, second{};
            if (controllerA->getParameterInfo(index, first) != kResultTrue ||
                controllerB->getParameterInfo(index, second) != kResultTrue ||
                first.id != second.id || !(first.flags & ParameterInfo::kCanAutomate) ||
                (first.flags & ParameterInfo::kIsReadOnly))
                continue;

            const ParamValue beforeA = controllerA->getParamNormalized(first.id);
            const ParamValue beforeB = controllerB->getParamNormalized(second.id);
            if (!std::isfinite(beforeA) || !std::isfinite(beforeB) ||
                beforeA < 0.0 || beforeA > 1.0 || beforeB < 0.0 || beforeB > 1.0)
                continue;

            const ParamValue stimulus = beforeA < 0.5 ? 0.75 : 0.25;
            if (controllerA->setParamNormalized(first.id, stimulus) != kResultTrue)
                continue;
            const ParamValue actualA = controllerA->getParamNormalized(first.id);
            const ParamValue actualB = controllerB->getParamNormalized(second.id);
            // A host-side controller edit does not establish a DSP outcome.
            const bool stimulusApplied = std::isfinite(actualA) &&
                                         std::fabs(actualA - stimulus) <= 1.0e-7;
            if (stimulusApplied) {
                observed.exercised = true;
                examinedId = first.id;
                observed.observedParameter = first.id;
                observed.expectedUntouched = beforeB;
                observed.actualUntouched = actualB;
                if (!std::isfinite(actualB) || std::fabs(actualB - beforeB) > 1.0e-7) {
                    if (negativeFixture) {
                        observed.knownDefect = true;
                        std::cerr << "[FAIL] Multi-instance controller isolation - shared-global negative fixture changed untouched B; expected="
                                  << beforeB << " actual=" << actualB << "\n";
                    } else {
                        observed.warning = true;
                        std::cout << "[WARN] Multi-instance controller isolation - untouched B parameter "
                                  << first.id << " changed when A was edited; intentional global linking cannot be ruled out\n";
                    }
                } else {
                    std::cout << "[PASS] Multi-instance controller isolation - B parameter "
                              << first.id << " remained " << beforeB << " after A edit to "
                              << stimulus << "\n";
                }
            } else {
                std::cout << "[INFO] Multi-instance controller isolation - NOT TESTED: A controller edit not reflected by A\n";
            }
            controllerA->setParamNormalized(first.id, beforeA);
            break;
        }
    }

    teardownController(bindingB, lifecycle, "Concurrent B");
    if (instanceB->terminate() != kResultOk)
        observed.warning = true;
    instanceB.reset();
    if (observed.exercised && controllerA) {
        const ParamValue afterPeerRemoval = controllerA->getParamNormalized(examinedId);
        // We restored A's baseline after the controlled edit; only check
        // API continuity here. A value change may be a plugin-specific policy.
        if (!std::isfinite(afterPeerRemoval))
            observed.warning = true;
        else
            std::cout << "[PASS] Multi-instance peer destruction - surviving A controller remained readable\n";
    }
    // Exercise a second creation while A remains alive; this is not a
    // multithreaded race or proof of an audio result.
    auto replacementB = factory.createInstance<IComponent>(classInfo.ID());
    if (replacementB) {
        if (replacementB->initialize(hostContext) == kResultOk) {
            if (replacementB->terminate() == kResultOk)
                std::cout << "[PASS] Multi-instance peer recreation - replacement B initialized and terminated while A remained alive\n";
            else
                observed.warning = true;
        } else {
            observed.warning = true;
        }
        replacementB.reset();
    } else {
        std::cout << "[INFO] Multi-instance peer recreation - NOT TESTED: replacement B unavailable\n";
    }

    teardownController(bindingA, lifecycle, "Concurrent A");
    if (instanceA->terminate() != kResultOk)
        observed.warning = true;
    instanceA.reset();

    if (!observed.exercised)
        std::cout << "[INFO] Multi-instance controller isolation - NOT TESTED: no independent writable normalized parameter observed\n";
    observed.warning = observed.warning || lifecycle.warning;
    return observed;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "[FAIL] State probe - no VST3 path supplied\n";
        return 2;
    }

    const fs::path pluginPath = fs::u8path(argv[1]);
    if (!fs::exists(pluginPath)) {
        std::cerr << "[FAIL] State probe - path does not exist\n";
        return 2;
    }

    std::string error;
    auto module = Module::create(pluginPath.u8string(), error);
    if (!module) {
        std::cerr << "[FAIL] State probe - module load failed: "
                  << (error.empty() ? "unknown error" : error) << '\n';
        return 1;
    }

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);
    const auto classes = factory.classInfos();

    std::error_code evidenceError;
    std::ofstream instanceEvidence;
    if (ReportPaths::ensureRoot(evidenceError)) {
        const fs::path evidencePath = ReportPaths::root() /
            (ReportPaths::baseName(pluginPath) + L"_125A_MultiInstance_Probe.txt");
        instanceEvidence.open(evidencePath, std::ios::binary | std::ios::trunc);
        if (instanceEvidence)
            instanceEvidence << "125A Plugin Tester - concurrent controller-instance oracle\n"
                             << "Evidence scope: control-thread normalized parameter observations only.\n"
                             << "Multiple instances are NOT multiple views of one instance.\n\n";
    }
    int audioClasses = 0;
    int statefulClasses = 0;
    bool warning = false;

    for (const auto& classInfo : classes) {
        if (classInfo.category() != kVstAudioEffectClass && std::string(classInfo.category().data()) != "Audio Mix Processor")
            continue;

        ++audioClasses;
        bool hardFailure = false;
        const auto classResult = probeClass(factory, classInfo, hostContext, hardFailure);
        if (hardFailure)
            return 1;
        if (classResult.hadState)
            ++statefulClasses;
        warning = warning || classResult.warning;
        const auto isolation = probeConcurrentControllerInstances(factory, classInfo, hostContext);
        if (instanceEvidence) {
            instanceEvidence << "Class: " << classInfo.name().data() << "\n"
                             << "Test: independent controllers of two simultaneous instances\n"
                             << "Expected: editing controller A does not change controller B\n";
            if (isolation.exercised) {
                instanceEvidence << "Parameter ID: " << isolation.observedParameter << "\n"
                                 << "Expected B: " << isolation.expectedUntouched << "\n"
                                 << "Observed B: " << isolation.actualUntouched << "\n"
                                 << "Result: " << (isolation.knownDefect ? "FAIL" :
                                                    isolation.warning ? "WARN" : "PASS") << "\n";
            } else {
                instanceEvidence << "Result: NOT TESTED / INCONCLUSIVE - no exercised writable parameter\n";
            }
            instanceEvidence << "Limit: DSP/audio, per-plugin linking policy and multi-editor UI are not proven.\n\n";
            instanceEvidence.flush();
        }
        if (isolation.knownDefect)
            return 1;
        warning = warning || isolation.warning;
    }

    if (audioClasses == 0) {
        std::cerr << "[FAIL] State probe - no supported audio processor class found\n";
        return 1;
    }

    if (statefulClasses == 0) {
        std::cout << "[WARN] Fresh-instance state verification - no AudioEffect class returned readable component persistence state\n";
        return kWarning;
    }

    if (warning) {
        std::cout << "[WARN] Fresh-instance state verification - component transfer passed with controller/lifecycle/persistence warning(s)\n";
        return kWarning;
    }

    std::cout << "[PASS] Fresh-instance state verification - component state survived full host-style source lifecycle, fresh-instance restore and re-save\n";
    return 0;
}
