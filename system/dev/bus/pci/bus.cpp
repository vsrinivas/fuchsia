#include "bus.h"
#include "config.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>

namespace pci {

zx_status_t Bus::Create(zx_device_t* parent) {
    zx_status_t status;
    pciroot_protocol_t pciroot;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &pciroot)) != ZX_OK) {
        zxlogf(ERROR, "%s: failed to obtain pciroot protocol: %d\n", "pci_bus", status);
        return status;
    }

    fbl::AllocChecker ac;
    Bus* bus = new (&ac) Bus(parent, &pciroot);
    if (!ac.check()) {
        zxlogf(ERROR, "%s: failed to allocate PciBus object.\n", "pci_bus");
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = bus->Init()) != ZX_OK) {
        zxlogf(ERROR, "%s: failed to initialize bus driver: %d\n", "pci_bus", status);
        return status;
    }

    return bus->DdkAdd("pci");
}

zx_status_t Bus::Init() {
    // Temporarily dump the config of bdf 00:00.0 to show proxy config
    // is working properly.
    pci_bdf_t bdf = {0, 0, 0};
    pciroot_protocol_t proto = {};
    GetProto(&proto);

    auto cfg = ProxyConfig::Create(bdf, &proto);
    if (cfg) {
        cfg->DumpConfig(PCI_BASE_CONFIG_SIZE);
    }
    return ZX_OK;
}

void Bus::DdkRelease() {
    delete this;
}

} // namespace pci

extern "C" zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent) {
    return pci::Bus::Create(parent);
}
