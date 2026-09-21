#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vsttypes.h"

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

    std::cout << "[PASS] Fresh-instance component state transfer - saved state applied after source instance destruction\n";
    std::cout << "[PASS] Fresh-instance state re-save - restored component returned state again ("
              << restoredState.getSize() << " bytes)\n";

    if (auto* activeController = destinationBinding.active()) {
        restoredState.seek(0, IBStream::kIBSeekSet, nullptr);
        if (activeController->setComponentState(&restoredState) == kResultTrue)
            std::cout << "[PASS] Fresh-instance controller state sync - controller accepted restored component state\n";
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
