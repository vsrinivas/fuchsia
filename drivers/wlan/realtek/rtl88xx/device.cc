#include "device.h"

#include <ddk/debug.h>
#include <zircon/errors.h>

namespace wlan {
namespace rtl88xx {

Device::~Device() {}

// static
zx_status_t Device::Create(std::unique_ptr<Bus> bus, std::unique_ptr<Device>* device) {
    zxlogf(ERROR, "rtl88xx: Device::Create() not implemented\n");
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace rtl88xx
}  // namespace wlan
