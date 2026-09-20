#ifndef BMC64_VICE_API_H
#define BMC64_VICE_API_H

class ViceOptions;

namespace bmc64 {
struct MachineDescriptor;

namespace vice {

void RunMainProgram(const char *timing_option,
                    ViceOptions *options,
                    const MachineDescriptor &machine);

}  // namespace vice
}  // namespace bmc64

#endif
