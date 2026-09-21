#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {

constexpr int kReloadCycles = 5;

bool runCycle(const fs::path& pluginPath, int cycle) {
    std::string error;
    auto module = Module::create(pluginPath.u8string(), error);
    if (!module) {
        std::cerr << "[FAIL] Reload cycle " << cycle << " - module load failed: "
                  << (error.empty() ? "unknown error" : error) << '\n';
        return false;
    }

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);
    const auto classes = factory.classInfos();

    int audioClasses = 0;
    for (const auto& classInfo : classes) {
        if (classInfo.category() != kVstAudioEffectClass && std::string(classInfo.category().data()) != "Audio Mix Processor")
            continue;

        ++audioClasses;
        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            std::cerr << "[FAIL] Reload cycle " << cycle << " - component creation failed\n";
            return false;
        }

        TUID controllerCID{};
        const bool hasControllerCID = component->getControllerClassId(controllerCID) == kResultTrue;

        if (component->initialize(hostContext) != kResultOk) {
            std::cerr << "[FAIL] Reload cycle " << cycle << " - component initialize failed\n";
            return false;
        }

        IPtr<IEditController> separateController;
        bool controllerInitialized = false;
        if (hasControllerCID) {
            separateController = factory.createInstance<IEditController>(VST3::UID(controllerCID));
            if (separateController) {
                if (separateController->initialize(hostContext) != kResultOk) {
                    component->terminate();
                    std::cerr << "[FAIL] Reload cycle " << cycle << " - controller initialize failed\n";
                    return false;
                }
                controllerInitialized = true;
            }
        }

        if (controllerInitialized && separateController->terminate() != kResultOk) {
            component->terminate();
            std::cerr << "[FAIL] Reload cycle " << cycle << " - controller terminate failed\n";
            return false;
        }

        if (component->terminate() != kResultOk) {
            std::cerr << "[FAIL] Reload cycle " << cycle << " - component terminate failed\n";
            return false;
        }
    }

    if (audioClasses == 0) {
        std::cerr << "[FAIL] Reload cycle " << cycle << " - no supported audio processor class found\n";
        return false;
    }

    std::cout << "[PASS] Reload cycle " << cycle << " - module/component/controller lifecycle completed\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "[FAIL] Reload probe - no VST3 path supplied\n";
        return 2;
    }

    const fs::path pluginPath = fs::u8path(argv[1]);
    if (!fs::exists(pluginPath)) {
        std::cerr << "[FAIL] Reload probe - path does not exist\n";
        return 2;
    }

    for (int cycle = 1; cycle <= kReloadCycles; ++cycle) {
        if (!runCycle(pluginPath, cycle))
            return 1;
    }

    std::cout << "[PASS] Reload/instantiation stress - " << kReloadCycles
              << " complete module reload and instance lifecycle cycles passed\n";
    return 0;
}
