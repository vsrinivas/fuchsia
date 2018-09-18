#include "bus.h"

#include <zircon/errors.h>

namespace wlan {
namespace rtl88xx {

Bus::~Bus() {}

// static
zx_status_t Bus::Create(zx_device_t* bus_device, std::unique_ptr<Bus>* bus) {
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace rtl88xx
}  // namespace wlan
