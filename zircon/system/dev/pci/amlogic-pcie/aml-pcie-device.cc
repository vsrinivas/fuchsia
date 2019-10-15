// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pcie-device.h"

#include <lib/zx/bti.h>
#include <zircon/driver/binding.h>

#include <memory>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "aml-pcie-clk.h"
#include "aml-pcie.h"

namespace pcie {
namespace aml {

namespace {
struct FreeDeleter {
  void operator()(void* ptr) const { ::free(ptr); }
};
}  // namespace

const size_t kElbMmio = 0;
const size_t kCfgMmio = 1;
const size_t kRstMmio = 2;
const size_t kPllMmio = 3;

zx_status_t AmlPcieDevice::InitProtocols() {
  ddk::CompositeProtocolClient composite(parent_);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "ZX_PROTOCOL_COMPOSITE not available\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Zeroth component is pdev, first is GPIO
  zx_device_t* components[kClockCount + 2];
  size_t actual;
  composite.GetComponents(components, fbl::count_of(components), &actual);
  if (actual != fbl::count_of(components)) {
    zxlogf(ERROR, "could not retrieve all our components\n");
    return ZX_ERR_INTERNAL;
  }

  auto st = device_get_protocol(components[0], ZX_PROTOCOL_PDEV, &pdev_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to get pdev protocol, st = %d", st);
    return st;
  }

  st = device_get_protocol(components[1], ZX_PROTOCOL_GPIO, &gpio_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to get gpio protocol, st = %d", st);
    return st;
  }

  st = gpio_config_out(&gpio_, 0);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to configure rst gpio, st = %d", st);
    return st;
  }

  for (unsigned i = 0; i < kClockCount; i++) {
    auto status = device_get_protocol(components[i + 2], ZX_PROTOCOL_CLOCK, &clks_[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml_pcie: failed to get clk protocol\n");
      return status;
    }
  }

  return st;
}

zx_status_t AmlPcieDevice::InitMmios() {
  zx_status_t st;

  // Get a BTI for pinning the DBI.
  zx::bti pin_bti;
  pdev_get_bti(&pdev_, 0, pin_bti.reset_and_get_address());

  mmio_buffer_t mmio;
  st = pdev_map_mmio_buffer(&pdev_, kElbMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to map dbi mmio, st = %d\n", st);
    return st;
  }
  dbi_ = ddk::MmioBuffer(mmio);

  std::optional<ddk::MmioPinnedBuffer> mmio_pinned;
  st = dbi_->Pin(pin_bti, &mmio_pinned);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to pin DBI, st = %d\n", st);
    return st;
  }
  dbi_pinned_ = *std::move(mmio_pinned);

  st = pdev_map_mmio_buffer(&pdev_, kCfgMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to map cfg mmio, st = %d\n", st);
    return st;
  }
  cfg_ = ddk::MmioBuffer(mmio);

  st = pdev_map_mmio_buffer(&pdev_, kRstMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to map rst mmio, st = %d\n", st);
    return st;
  }
  rst_ = ddk::MmioBuffer(mmio);

  st = pdev_map_mmio_buffer(&pdev_, kPllMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to map pll mmio, st = %d\n", st);
    return st;
  }
  pll_ = ddk::MmioBuffer(mmio);

  return st;
}

zx_status_t AmlPcieDevice::InitMetadata() {
  zx_status_t st;
  size_t actual;

  st = device_get_metadata(parent_, IATU_CFG_APERTURE_METADATA, &atu_cfg_, sizeof(atu_cfg_),
                           &actual);
  if (st != ZX_OK || actual != sizeof(atu_cfg_)) {
    zxlogf(ERROR, "aml_pcie: could not get cfg atu metadata\n");
    return st;
  }

  st = device_get_metadata(parent_, IATU_IO_APERTURE_METADATA, &atu_io_, sizeof(atu_io_), &actual);
  if (st != ZX_OK || actual != sizeof(atu_io_)) {
    zxlogf(ERROR, "aml_pcie: could not get io atu metadata\n");
    return st;
  }

  st = device_get_metadata(parent_, IATU_MMIO_APERTURE_METADATA, &atu_mem_, sizeof(atu_mem_),
                           &actual);
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
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_KPCI},
};

device_add_args_t pci_dev_args = []() {
  device_add_args_t result;

  result.version = DEVICE_ADD_ARGS_VERSION;
  result.name = "aml-dw-pcie";
  result.ops = &aml_pcie_device_proto, result.props = props;
  result.prop_count = fbl::count_of(props);

  return result;
}();

zx_status_t AmlPcieDevice::Init() {
  zx_status_t st;

  st = InitProtocols();
  if (st != ZX_OK)
    return st;

  st = InitMmios();
  if (st != ZX_OK)
    return st;

  st = InitMetadata();
  if (st != ZX_OK)
    return st;

  pcie_ = std::make_unique<AmlPcie>(*std::move(dbi_), *std::move(cfg_), *std::move(rst_),
                                    1  // Single Lane PCIe
  );

  pcie_->AssertReset(kRstPcieA | kRstPcieB | kRstPcieApb | kRstPciePhy);

  PllSetRate(&*pll_);

  pcie_->ClearReset(kRstPcieApb | kRstPciePhy);

  st = clock_enable(&clks_[kClk81]);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to init root clock, st = %d\n", st);
    return st;
  }

  st = clock_enable(&clks_[kClkPcieA]);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to init pciea clock, st = %d\n", st);
    return st;
  }

  pcie_->ClearReset(kRstPcieA);

  st = clock_enable(&clks_[kClkPort]);
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

  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_pci_add_subtract_io_range(get_root_resource(), false, atu_io_.cpu_addr, atu_io_.length,
                                    true);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to add pcie io range, st = %d\n", st);
    return st;
  }

  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_pci_add_subtract_io_range(get_root_resource(), true, atu_mem_.cpu_addr, atu_mem_.length,
                                    true);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to add pcie mmio range, st = %d\n", st);
    return st;
  }

  // Fire up the kernel PCI driver!
  zx_pci_init_arg_t* arg;
  const size_t arg_size = sizeof(*arg) + sizeof(arg->addr_windows[0]) * 2;
  arg = (zx_pci_init_arg_t*)calloc(1, arg_size);
  if (!arg) {
    zxlogf(ERROR, "aml_pcie: failed to allocate pci init arg\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Automatically release this object when it goes out of scope.
  std::unique_ptr<zx_pci_init_arg_t, FreeDeleter> deleter;
  deleter.reset(arg);

  arg->num_irqs = 0;
  arg->addr_window_count = 2;

  // Root Bridge Config Window.
  arg->addr_windows[0].cfg_space_type = PCI_CFG_SPACE_TYPE_DW_ROOT;
  arg->addr_windows[0].has_ecam = true;
  arg->addr_windows[0].base = dbi_pinned_->get_paddr();
  arg->addr_windows[0].size = 4 * 1024;  // Just enough for CFG 0:0.0
  arg->addr_windows[0].bus_start = 0;
  arg->addr_windows[0].bus_end = 0;

  // Downstream Config Window.
  arg->addr_windows[1].cfg_space_type = PCI_CFG_SPACE_TYPE_DW_DS;
  arg->addr_windows[1].has_ecam = true;
  arg->addr_windows[1].base = atu_cfg_.cpu_addr;
  arg->addr_windows[1].size = atu_cfg_.length;
  arg->addr_windows[1].bus_start = 1;
  arg->addr_windows[1].bus_end = 1;

  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_pci_init(get_root_resource(), arg, arg_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml_pcie: failed to init pci bus driver, st = %d\n", st);
    return st;
  }

  pci_dev_args.ctx = (void*)this;

  dev_ = nullptr;
  /* FIXME this needs to be rewritten to use composite devices
      st = pdev_device_add(&pdev_, 0, &pci_dev_args, &dev_);
      if (st != ZX_OK) {
          zxlogf(ERROR, "aml_pcie: pdev_device_add failed, st = %d\n", st);
          return st;
      }
  */
  st = ZX_ERR_NOT_SUPPORTED;

  return st;
}

}  // namespace aml
}  // namespace pcie

extern "C" zx_status_t aml_pcie_bind(void* ctx, zx_device_t* device) {
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

static constexpr zx_driver_ops_t aml_pcie_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_pcie_bind;
  return ops;
}();

// clang-format off
// Bind to ANY Amlogic SoC with a DWC PCIe controller.
ZIRCON_DRIVER_BEGIN(aml_pcie, aml_pcie_driver_ops, "zircon", "0.1", 4)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DW_PCIE),
ZIRCON_DRIVER_END(aml_pcie)
