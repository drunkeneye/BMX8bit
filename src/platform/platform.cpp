#include "platform/platform.h"
#include "update/generated/update_path_policy_v1.h"

namespace bmc64 {

namespace {

namespace path_policy = bmx::update::generated_path_policy_v1;

static_assert(path_policy::kMachineKernelBaseCount == 2U,
              "platform requires pi4 and pi5 kernel bases");

#if RASPPI == 5
const PlatformDescriptor kPlatform = {
    "pi5",
    5,
    false,
    true,
    path_policy::kMachineKernelBases[1U],
};
#else
const PlatformDescriptor kPlatform = {
    "pi4",
    4,
    false,
    false,
    path_policy::kMachineKernelBases[0U],
};
#endif

}  // namespace

const PlatformDescriptor &CurrentPlatform() {
  return kPlatform;
}

int VolumePercentToDeviceControl(int percent) {
  return percent;
}

}  // namespace bmc64
