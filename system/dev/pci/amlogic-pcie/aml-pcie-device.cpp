// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "aml-pcie-device.h"

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <fbl/algorithm.h>
#include <zircon/driver/binding.h>

#include "aml-pcie-clk.h"
#include "aml-pcie.h"

namespace pcie {
namespace aml {

const size_t kElbMmio = 0;
const size_t kCfgMmio = 1;
const size_t kRstMmio = 2;
const size_t kPllMmio = 3;

const size_t kClk81 = 0;
const size_t kClkPcieA = 1;
const size_t kClkPort = 2;

zx_status_t AmlPcieDevice::InitProtocols() {
    zx_status_t st;

    st = device_get_protocol(parent_, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to get pdev protocol, st = %d", st);
        return st;
    }

    st = device_get_protocol(parent_, ZX_PROTOCOL_GPIO, &gpio_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to get gpio protocol, st = %d", st);
        return st;
    }

    st = gpio_config_out(&gpio_, 0);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to configure rst gpio, st = %d", st);
        return st;
    }

    st = device_get_protocol(parent_, ZX_PROTOCOL_CLK, &clk_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to get clk protocol, st = %d", st);
        return st;
    }

    return st;
}

zx_status_t AmlPcieDevice::InitMmios() {
    zx_status_t st;

    st = pdev_map_mmio_buffer(&pdev_, kElbMmio,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &dbi_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to map elbi mmio, st = %d\n", st);
        return st;
    }

    st = pdev_map_mmio_buffer(&pdev_, kCfgMmio,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &cfg_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to map cfg mmio, st = %d\n", st);
        return st;
    }

    st = pdev_map_mmio_buffer(&pdev_, kRstMmio,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &rst_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to map rst mmio, st = %d\n", st);
        return st;
    }

    st = pdev_map_mmio_buffer(&pdev_, kPllMmio,
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &pll_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to map pll mmio, st = %d\n", st);
        return st;
    }

    return st;
}

zx_status_t AmlPcieDevice::InitMetadata() {
    zx_status_t st;
    size_t actual;

    st = device_get_metadata(parent_, IATU_CFG_APERTURE_METADATA, &atu_cfg_,
                             sizeof(atu_cfg_), &actual);
    if (st != ZX_OK || actual != sizeof(atu_cfg_)) {
        zxlogf(ERROR, "aml_pcie: could not get cfg atu metadata\n");
        return st;
    }

    st = device_get_metadata(parent_, IATU_IO_APERTURE_METADATA, &atu_io_,
                             sizeof(atu_io_), &actual);
    if (st != ZX_OK || actual != sizeof(atu_io_)) {
        zxlogf(ERROR, "aml_pcie: could not get io atu metadata\n");
        return st;
    }

    st = device_get_metadata(parent_, IATU_MMIO_APERTURE_METADATA, &atu_mem_,
                             sizeof(atu_mem_), &actual);
    if (st != ZX_OK || actual != sizeof(atu_mem_)) {
        zxlogf(ERROR, "aml_pcie: could not get mem atu metadata\n");
        return st;
    }

    return st;
}

static void aml_pcie_release(void* ctx) {
    AmlPcieDevice* self = reinterpret_cast<AmlPcieDevice*>(ctx);

    delete self;
}

static zx_protocol_device_t aml_pcie_device_proto = []() {
    zx_protocol_device_t result;
    result.version = DEVICE_OPS_VERSION;
    result.release = aml_pcie_release;
    return result;
}();

zx_device_prop_t props[] = {
    { BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC },
    { BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC },
    { BIND_PLATFORM_DEV_DID, 0, PDEV_DID_KPCI },
};

device_add_args_t pci_dev_args = []() {
    device_add_args_t result;

    result.version = DEVICE_ADD_ARGS_VERSION;
    result.name = "aml-dw-pcie";
    result.ops = &aml_pcie_device_proto,
    result.props = props;
    result.prop_count = fbl::count_of(props);

    return result;
}();

zx_status_t AmlPcieDevice::Init() {
    zx_status_t st;

    st = InitProtocols();
    if (st != ZX_OK) return st;

    st = InitMmios();
    if (st != ZX_OK) return st;

    st = InitMetadata();
    if (st != ZX_OK) return st;

    pcie_ = fbl::make_unique<AmlPcie>(
        io_buffer_virt(&dbi_),
        io_buffer_virt(&cfg_),
        io_buffer_virt(&rst_),
        1   // Single Lane PCIe
    );

    pcie_->AssertReset(kRstPcieA | kRstPcieB | kRstPcieApb | kRstPciePhy);

    PllSetRate((zx_vaddr_t)io_buffer_virt(&pll_));

    pcie_->ClearReset(kRstPcieApb | kRstPciePhy);

    st = clk_enable(&clk_, kClk81);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to init root clock, st = %d\n", st);
        return st;
    }

    st = clk_enable(&clk_, kClkPcieA);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to init pciea clock, st = %d\n", st);
        return st;
    }

    pcie_->ClearReset(kRstPcieA);

    st = clk_enable(&clk_, kClkPort);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to init port clock, st = %d\n", st);
        return st;
    }

    // Whack the reset gpio.
    gpio_write(&gpio_, 0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    gpio_write(&gpio_, 1);

    st = pcie_->EstablishLink(&atu_cfg_, &atu_io_, &atu_mem_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to establish link, st = %d\n", st);
        return st;
    }

    st = zx_pci_add_subtract_io_range(get_root_resource(), false,
                                      atu_io_.cpu_addr, atu_io_.length, true);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to add pcie io range, st = %d\n", st);
        return st;
    }

    st = zx_pci_add_subtract_io_range(get_root_resource(), true,
                                      atu_mem_.cpu_addr, atu_mem_.length, true);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to add pcie mmio range, st = %d\n", st);
        return st;
    }

    // Fire up the kernel PCI driver!
    zx_pci_init_arg_t* arg;
    const size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]);
    arg = (zx_pci_init_arg_t*)calloc(1, arg_size);
    if (!arg) {
        zxlogf(ERROR, "aml_pcie: failed to allocate pci init arg\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Automatically release this object when it goes out of scope.
    fbl::unique_ptr<zx_pci_init_arg_t> deleter;
    deleter.reset(arg);

    arg->num_irqs = 0;
    arg->addr_window_count = 1;
    arg->addr_windows[0].is_mmio = true;
    arg->addr_windows[0].has_ecam = true;
    arg->addr_windows[0].base = atu_cfg_.cpu_addr;
    arg->addr_windows[0].size = 1 * 1024 * 1024;
    arg->addr_windows[0].bus_start = 0;
    arg->addr_windows[0].bus_end = 0xff;

    st = zx_pci_init(get_root_resource(), arg, arg_size);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to init pci bus driver, st = %d\n", st);
        return st;
    }

    pci_dev_args.ctx = (void*)this;

    st = pdev_device_add(&pdev_, 0, &pci_dev_args, &dev_);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: pdev_device_add failed, st = %d\n", st);
        return st;
    }

    return st;
}

AmlPcieDevice::~AmlPcieDevice() {
    io_buffer_release(&dbi_);
    io_buffer_release(&cfg_);
    io_buffer_release(&rst_);
    io_buffer_release(&pll_);
}

}  // namespace aml
}  // namespace pcie

extern "C" zx_status_t aml_pcie_bind(void* ctx, zx_device_t* device, void** cookie) {
    fbl::AllocChecker ac;
    pcie::aml::AmlPcieDevice* dev = new (&ac) pcie::aml::AmlPcieDevice(device);

    if (!ac.check()) {
        zxlogf(ERROR, "aml_pcie: failed to allocate aml pcie device\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Note: dev is leaked if the driver successfully binds since devmgr now
    // owns the memory.
    zx_status_t st = dev->Init();
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml_pcie: failed to start, st = %d\n", st);
        delete dev;
    }

    return st;
}
