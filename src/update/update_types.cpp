#include "update_types.h"

namespace bmx {
namespace update {

bool IsKnownBoardFamily(BoardFamily board)
{
    return board == BoardFamily::Pi4Pi400 || board == BoardFamily::Pi5Pi500;
}

}  // namespace update
}  // namespace bmx
