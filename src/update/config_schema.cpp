#include "config_schema.h"

namespace bmx {
namespace update {

bool IsKnownConfigArea(ConfigArea area)
{
    return area == ConfigArea::Machines ||
           area == ConfigArea::ConfigManagedBlock ||
           area == ConfigArea::CmdlineManagedKeys ||
           area == ConfigArea::Network || area == ConfigArea::Settings ||
           area == ConfigArea::UpdateState;
}

}  // namespace update
}  // namespace bmx
