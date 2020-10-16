// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <err.h>
#include <lib/pci/pio.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/syscalls/pci.h>

#include <dev/interrupt.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <ktl/algorithm.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/unique_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_physical.h>

#include "priv.h"

#define LOCAL_TRACE 0

// If we were built with the GFX console, make sure that it is un-bound when
// user mode takes control of PCI.  Note: there should probably be a cleaner way
// of doing this.  Not all system have PCI, and (eventually) not all systems
// will attempt to initialize PCI.  Someday, there should be a different way of
// handing off from early/BSOD kernel mode graphics to user mode.
#include <lib/gfxconsole.h>
static inline void shutdown_early_init_console() { gfxconsole_bind_display(nullptr, nullptr); }

#ifdef WITH_KERNEL_PCIE
#include <dev/address_provider/ecam_region.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_root.h>
#include <object/pci_device_dispatcher.h>

namespace {
struct FreeDeleter {
  void operator()(void* ptr) const { ::free(ptr); }
};
}  // namespace

// Implementation of a PcieRoot with a look-up table based legacy IRQ swizzler
// suitable for use with ACPI style swizzle definitions.
class PcieRootLUTSwizzle : public PcieRoot {
 public:
  static fbl::RefPtr<PcieRoot> Create(PcieBusDriver& bus_drv, uint managed_bus_id,
                                      const zx_pci_irq_swizzle_lut_t& lut) {
    fbl::AllocChecker ac;
    auto root = fbl::AdoptRef(new (&ac) PcieRootLUTSwizzle(bus_drv, managed_bus_id, lut));
    if (!ac.check()) {
      TRACEF("Out of memory attemping to create PCIe root to manage bus ID 0x%02x\n",
             managed_bus_id);
      return nullptr;
    }

    return root;
  }

  zx_status_t Swizzle(uint dev_id, uint func_id, uint pin, uint* irq) override {
    if ((irq == nullptr) || (dev_id >= ktl::size(lut_)) || (func_id >= ktl::size(lut_[dev_id])) ||
        (pin >= ktl::size(lut_[dev_id][func_id])))
      return ZX_ERR_INVALID_ARGS;

    *irq = lut_[dev_id][func_id][pin];
    return (*irq == ZX_PCI_NO_IRQ_MAPPING) ? ZX_ERR_NOT_FOUND : ZX_OK;
  }

 private:
  PcieRootLUTSwizzle(PcieBusDriver& bus_drv, uint managed_bus_id,
                     const zx_pci_irq_swizzle_lut_t& lut)
      : PcieRoot(bus_drv, managed_bus_id) {
    ::memcpy(&lut_, &lut, sizeof(lut_));
  }

  zx_pci_irq_swizzle_lut_t lut_;
};

// Scan |lut| for entries mapping to |irq|, and replace them with
// ZX_PCI_NO_IRQ_MAPPING.
static void pci_irq_swizzle_lut_remove_irq(zx_pci_irq_swizzle_lut_t* lut, uint32_t irq) {
  for (size_t dev = 0; dev < ktl::size(*lut); ++dev) {
    for (size_t func = 0; func < ktl::size((*lut)[dev]); ++func) {
      for (size_t pin = 0; pin < ktl::size((*lut)[dev][func]); ++pin) {
        uint32_t* assigned_irq = &(*lut)[dev][func][pin];
        if (*assigned_irq == irq) {
          *assigned_irq = ZX_PCI_NO_IRQ_MAPPING;
        }
      }
    }
  }
}

// zx_status_t zx_pci_add_subtract_io_range
zx_status_t sys_pci_add_subtract_io_range(zx_handle_t handle, uint32_t mmio, uint64_t base,
                                          uint64_t len, uint32_t add) {
  bool is_add = (add > 0);
  bool is_mmio = (mmio > 0);
  LTRACEF("handle %x mmio %d base %#" PRIx64 " len %#" PRIx64 " add %d\n", handle, is_mmio, base,
          len, is_add);

  // TODO(fxbug.dev/30918): finer grained validation
  // TODO(security): Add additional access checks
  zx_status_t status;
  if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }

  auto pcie = PcieBusDriver::GetDriver();
  if (pcie == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  PciAddrSpace addr_space = is_mmio ? PciAddrSpace::MMIO : PciAddrSpace::PIO;

  if (is_add) {
    return pcie->AddBusRegion(base, len, addr_space);
  } else {
    return pcie->SubtractBusRegion(base, len, addr_space);
  }
}

static inline PciEcamRegion addr_window_to_pci_ecam_region(zx_pci_init_arg_t* arg, size_t index) {
  ASSERT(index < arg->addr_window_count);

  const PciEcamRegion result = {
      .phys_base = static_cast<paddr_t>(arg->addr_windows[index].base),
      .size = arg->addr_windows[index].size,
      .bus_start = arg->addr_windows[index].bus_start,
      .bus_end = arg->addr_windows[index].bus_end,
  };

  return result;
}

static inline bool is_designware(const zx_pci_init_arg_t* arg) {
  for (size_t i = 0; i < arg->addr_window_count; i++) {
    if ((arg->addr_windows[i].cfg_space_type == PCI_CFG_SPACE_TYPE_DW_ROOT) ||
        (arg->addr_windows[i].cfg_space_type == PCI_CFG_SPACE_TYPE_DW_DS)) {
      return true;
    }
  }

  return false;
}

// zx_status_t zx_pci_init
zx_status_t sys_pci_init(zx_handle_t handle, user_in_ptr<const zx_pci_init_arg_t> _init_buf,
                         uint32_t len) {
  // TODO(fxbug.dev/30918): finer grained validation
  // TODO(security): Add additional access checks
  zx_status_t status;
  if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }

  ktl::unique_ptr<zx_pci_init_arg_t, FreeDeleter> arg;

  if (len < sizeof(*arg) || len > ZX_PCI_INIT_ARG_MAX_SIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto pcie = PcieBusDriver::GetDriver();
  if (pcie == nullptr)
    return ZX_ERR_BAD_STATE;

  // we have to malloc instead of new since this is a variable-sized structure
  arg.reset(static_cast<zx_pci_init_arg_t*>(malloc(len)));
  if (!arg) {
    return ZX_ERR_NO_MEMORY;
  }

  // Copy in the base struct.
  status = _init_buf.copy_from_user(arg.get());
  if (status != ZX_OK) {
    return status;
  }

  // Are there any flexible array members to copy in?
  const uint32_t win_count = arg->addr_window_count;
  if (len != sizeof(*arg) + sizeof(arg->addr_windows[0]) * win_count) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (win_count > 0) {
    // The flexible array member is an unnamed struct so use typedef so we make a user_ptr to it.
    typedef ktl::remove_reference_t<decltype(zx_pci_init_arg_t::addr_windows[0])> addr_window_t;
    user_in_ptr<const addr_window_t> addr_windows =
        _init_buf.byte_offset(sizeof(*arg)).reinterpret<const addr_window_t>();
    status = addr_windows.copy_array_from_user(arg->addr_windows, win_count);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (arg->num_irqs > ktl::size(arg->irqs)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (LOCAL_TRACE) {
    const char* kAddrWindowTypes[] = {"PIO", "MMIO", "DW Root Bridge (MMIO)",
                                      "DW Downstream (MMIO)", "Unknown"};
    TRACEF("%u address window%s found in init arg\n", arg->addr_window_count,
           (arg->addr_window_count == 1) ? "" : "s");
    for (uint32_t i = 0; i < arg->addr_window_count; i++) {
      const size_t window_type_idx =
          ktl::min(ktl::size(kAddrWindowTypes) - 1,
                   static_cast<size_t>(arg->addr_windows[i].cfg_space_type));
      const char* window_type_name = kAddrWindowTypes[window_type_idx];

      printf("[%u]\n\tcfg_space_type: %s\n\thas_ecam: %d\n\tbase: %#" PRIxPTR
             "\n"
             "\tsize: %zu\n\tbus_start: %u\n\tbus_end: %u\n",
             i, window_type_name, arg->addr_windows[i].has_ecam, arg->addr_windows[i].base,
             arg->addr_windows[i].size, arg->addr_windows[i].bus_start,
             arg->addr_windows[i].bus_end);
    }
  }

  // Configure interrupts
  for (unsigned int i = 0; i < arg->num_irqs; ++i) {
    uint32_t irq = arg->irqs[i].global_irq;
    if (!is_valid_interrupt(irq, 0)) {
      // If the interrupt isn't valid, mask it out of the IRQ swizzle table
      // and don't activate it.  Attempts to use legacy IRQs for the device
      // will fail later.
      arg->irqs[i].global_irq = ZX_PCI_NO_IRQ_MAPPING;
      pci_irq_swizzle_lut_remove_irq(&arg->dev_pin_to_global_irq, irq);
      continue;
    }

    enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
    if (arg->irqs[i].level_triggered) {
      tm = IRQ_TRIGGER_MODE_LEVEL;
    }
    enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
    if (arg->irqs[i].active_high) {
      pol = IRQ_POLARITY_ACTIVE_HIGH;
    }

    zx_status_t status = configure_interrupt(irq, tm, pol);
    if (status != ZX_OK) {
      return status;
    }
  }
  // TODO(teisenbe): For now assume there is only one ECAM, unless it's a
  // DesignWare Controller.
  // The DesignWare controller needs exactly two windows: One specifying where
  // the root bridge is and the other specifying where the downstream devices
  // are.
  if (is_designware(arg.get())) {
    if (win_count != 2) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    if (win_count != 1) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (arg->addr_windows[0].bus_start != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (arg->addr_windows[0].bus_start > arg->addr_windows[0].bus_end) {
    return ZX_ERR_INVALID_ARGS;
  }

#if ARCH_X86
  // Check for a quirk that we've seen.  Some systems will report overly large
  // PCIe config regions that collide with architectural registers.
  unsigned int num_buses = arg->addr_windows[0].bus_end - arg->addr_windows[0].bus_start + 1;
  paddr_t end = arg->addr_windows[0].base + num_buses * PCIE_ECAM_BYTE_PER_BUS;
  const paddr_t high_limit = 0xfec00000ULL;
  if (end > high_limit) {
    TRACEF("PCIe config space collides with arch devices, truncating\n");
    end = high_limit;
    if (end < arg->addr_windows[0].base) {
      return ZX_ERR_INVALID_ARGS;
    }
    arg->addr_windows[0].size = ROUNDDOWN(end - arg->addr_windows[0].base, PCIE_ECAM_BYTE_PER_BUS);
    uint64_t new_bus_end =
        (arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) + arg->addr_windows[0].bus_start - 1;
    if (new_bus_end >= PCIE_MAX_BUSSES) {
      return ZX_ERR_INVALID_ARGS;
    }
    arg->addr_windows[0].bus_end = static_cast<uint8_t>(new_bus_end);
  }
#endif

  if (arg->addr_windows[0].cfg_space_type == PCI_CFG_SPACE_TYPE_MMIO) {
    if (arg->addr_windows[0].size < PCIE_ECAM_BYTE_PER_BUS) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS >
        PCIE_MAX_BUSSES - arg->addr_windows[0].bus_start) {
      return ZX_ERR_INVALID_ARGS;
    }

    // TODO(johngro): Update the syscall to pass a paddr_t for base instead of a uint64_t
    ASSERT(arg->addr_windows[0].base < ktl::numeric_limits<paddr_t>::max());

    fbl::AllocChecker ac;
    auto addr_provider = ktl::make_unique<MmioPcieAddressProvider>(&ac);
    if (!ac.check()) {
      TRACEF("Failed to allocate PCIe Address Provider\n");
      return ZX_ERR_NO_MEMORY;
    }

    // TODO(johngro): Do not limit this to a single range.  Instead, fetch all
    // of the ECAM ranges from ACPI, as well as the appropriate bus start/end
    // ranges.
    const PciEcamRegion ecam = {
        .phys_base = static_cast<paddr_t>(arg->addr_windows[0].base),
        .size = arg->addr_windows[0].size,
        .bus_start = 0x00,
        .bus_end = static_cast<uint8_t>((arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) - 1),
    };

    zx_status_t ret = addr_provider->AddEcamRegion(ecam);
    if (ret != ZX_OK) {
      TRACEF("Failed to add ECAM region to PCIe bus driver! (ret %d)\n", ret);
      return ret;
    }

    ret = pcie->SetAddressTranslationProvider(ktl::move(addr_provider));
    if (ret != ZX_OK) {
      TRACEF("Failed to set Address Translation Provider, st = %d\n", ret);
      return ret;
    }
  } else if (arg->addr_windows[0].cfg_space_type == PCI_CFG_SPACE_TYPE_PIO) {
    // Create a PIO address provider.
    fbl::AllocChecker ac;

    auto addr_provider = ktl::make_unique<PioPcieAddressProvider>(&ac);
    if (!ac.check()) {
      TRACEF("Failed to allocate PCIe address provider\n");
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t ret = pcie->SetAddressTranslationProvider(ktl::move(addr_provider));
    if (ret != ZX_OK) {
      TRACEF("Failed to set Address Translation Provider, st = %d\n", ret);
      return ret;
    }
  } else if (is_designware(arg.get())) {
    fbl::AllocChecker ac;

    if (win_count < 2) {
      TRACEF("DesignWare Config Space requires at least 2 windows\n");
      return ZX_ERR_INVALID_ARGS;
    }

    auto addr_provider = ktl::make_unique<DesignWarePcieAddressProvider>(&ac);
    if (!ac.check()) {
      TRACEF("Failed to allocate PCIe address provider\n");
      return ZX_ERR_NO_MEMORY;
    }

    PciEcamRegion dw_root_bridge{};
    PciEcamRegion dw_downstream{};
    for (size_t i = 0; i < win_count; i++) {
      switch (arg->addr_windows[i].cfg_space_type) {
        case PCI_CFG_SPACE_TYPE_DW_ROOT:
          dw_root_bridge = addr_window_to_pci_ecam_region(arg.get(), i);
          break;
        case PCI_CFG_SPACE_TYPE_DW_DS:
          dw_downstream = addr_window_to_pci_ecam_region(arg.get(), i);
          break;
      }
    }

    if (dw_root_bridge.size == 0 || dw_downstream.size == 0) {
      TRACEF("Did not find DesignWare root and downstream device in init arg\n");
      return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t ret = addr_provider->Init(dw_root_bridge, dw_downstream);
    if (ret != ZX_OK) {
      TRACEF("Failed to initialize DesignWare PCIe Address Provider, error = %d\n", ret);
      return ret;
    }

    ret = pcie->SetAddressTranslationProvider(ktl::move(addr_provider));
    if (ret != ZX_OK) {
      TRACEF("Failed to set Address Translation Provider, st = %d\n", ret);
      return ret;
    }

  } else {
    TRACEF("Unknown config space type!\n");
    return ZX_ERR_INVALID_ARGS;
  }
  // TODO(johngro): Change the user-mode and devmgr behavior to add all of the
  // roots in the system.  Do not assume that there is a single root, nor that
  // it manages bus ID 0.
  auto root = PcieRootLUTSwizzle::Create(*pcie, 0, arg->dev_pin_to_global_irq);
  if (root == nullptr)
    return ZX_ERR_NO_MEMORY;

  zx_status_t ret = pcie->AddRoot(ktl::move(root));
  if (ret != ZX_OK) {
    TRACEF("Failed to add root complex to PCIe bus driver! (ret %d)\n", ret);
    return ret;
  }

  ret = pcie->StartBusDriver();
  if (ret != ZX_OK) {
    TRACEF("Failed to start PCIe bus driver! (ret %d)\n", ret);
    return ret;
  }

  shutdown_early_init_console();
  return ZX_OK;
}

// zx_status_t zx_pci_get_nth_device
zx_status_t sys_pci_get_nth_device(zx_handle_t hrsrc, uint32_t index,
                                   user_out_ptr<zx_pcie_device_info_t> out_info,
                                   user_out_handle* out_handle) {
  /**
   * Returns the pci config of a device.
   * @param index Device index
   * @param out_info Device info (BDF address, vendor id, etc...)
   */
  LTRACEF("handle %x index %u\n", hrsrc, index);

  // TODO(fxbug.dev/30918): finer grained validation
  zx_status_t status;
  if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
    return status;
  }

  if (!out_info) {
    return ZX_ERR_INVALID_ARGS;
  }

  KernelHandle<PciDeviceDispatcher> handle;
  zx_rights_t rights;
  zx_pcie_device_info_t info{};
  status = PciDeviceDispatcher::Create(index, &info, &handle, &rights);
  if (status != ZX_OK) {
    return status;
  }

  // If everything is successful add the handle to the process
  status = out_info.copy_to_user(info);
  if (status != ZX_OK)
    return status;

  return out_handle->make(ktl::move(handle), rights);
}

// zx_status_t zx_pci_config_read
zx_status_t sys_pci_config_read(zx_handle_t handle, uint16_t offset, size_t width,
                                user_out_ptr<uint32_t> out_val) {
  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  fbl::RefPtr<Dispatcher> dispatcher;

  // Get the PciDeviceDispatcher from the handle passed in via the pci protocol
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->handle_table().GetDispatcherWithRights(
      handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK) {
    return status;
  }

  auto device = pci_device->device();
  auto cfg_size = device->is_pcie() ? PCIE_EXTENDED_CONFIG_SIZE : PCIE_BASE_CONFIG_SIZE;
  if (out_val.get() == nullptr || offset + width > cfg_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Based on the width passed in we can use the type safety of the PciConfig layer
  // to ensure we're getting correctly sized data back and return errors in the PIO
  // cases.
  auto config = device->config();
  switch (width) {
    case 1u:
      return out_val.copy_to_user(static_cast<uint32_t>(config->Read(PciReg8(offset))));
    case 2u:
      return out_val.copy_to_user(static_cast<uint32_t>(config->Read(PciReg16(offset))));
    case 4u:
      return out_val.copy_to_user(config->Read(PciReg32(offset)));
  }

  // If we reached this point then the width was invalid.
  return ZX_ERR_INVALID_ARGS;
}

// zx_status_t zx_pci_config_write
zx_status_t sys_pci_config_write(zx_handle_t handle, uint16_t offset, size_t width, uint32_t val) {
  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  fbl::RefPtr<Dispatcher> dispatcher;

  // Get the PciDeviceDispatcher from the handle passed in via the pci protocol
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->handle_table().GetDispatcherWithRights(
      handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK) {
    return status;
  }

  // Writes to the PCI header or outside of the device's config space are not allowed.
  auto device = pci_device->device();
  auto cfg_size = device->is_pcie() ? PCIE_EXTENDED_CONFIG_SIZE : PCIE_BASE_CONFIG_SIZE;
  if (offset < ZX_PCI_STANDARD_CONFIG_HDR_SIZE || offset + width > cfg_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto config = device->config();
  switch (width) {
    case 1u:
      config->Write(PciReg8(offset), static_cast<uint8_t>(val & UINT8_MAX));
      break;
    case 2u:
      config->Write(PciReg16(offset), static_cast<uint16_t>(val & UINT16_MAX));
      break;
    case 4u:
      config->Write(PciReg32(offset), val);
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}
/* This is a transitional method to bootstrap legacy PIO access before
 * PCI moves to userspace.
 */
// zx_status_t zx_pci_cfg_pio_rw
zx_status_t sys_pci_cfg_pio_rw(zx_handle_t handle, uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, user_inout_ptr<uint32_t> val, size_t width,
                               uint32_t write) {
#if ARCH_X86
  uint32_t val_;
  zx_status_t status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
  if (status != ZX_OK) {
    return status;
  }

  bool is_write = (write > 0);
  if (is_write) {
    status = val.copy_from_user(&val_);
    if (status != ZX_OK) {
      return status;
    }
    status = Pci::PioCfgWrite(bus, dev, func, offset, val_, width);
  } else {
    status = Pci::PioCfgRead(bus, dev, func, offset, &val_, width);
    if (status == ZX_OK) {
      status = val.copy_to_user(val_);
    }
  }

  return status;
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

// zx_status_t zx_pci_enable_bus_master
zx_status_t sys_pci_enable_bus_master(zx_handle_t dev_handle, uint32_t enable) {
  /**
   * Enables or disables bus mastering for the PCI device associated with the handle.
   * @param handle Handle associated with a PCI device
   * @param enable true if bus mastering should be enabled.
   */
  LTRACEF("handle %x\n", dev_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK)
    return status;

  return pci_device->EnableBusMaster(enable > 0);
}

// zx_status_t zx_pci_reset_device
zx_status_t sys_pci_reset_device(zx_handle_t dev_handle) {
  /**
   * Resets the PCI device associated with the handle.
   * @param handle Handle associated with a PCI device
   */
  LTRACEF("handle %x\n", dev_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK)
    return status;

  return pci_device->ResetDevice();
}

// zx_status_t zx_pci_get_bar
zx_status_t sys_pci_get_bar(zx_handle_t dev_handle, uint32_t bar_num,
                            user_out_ptr<zx_pci_bar_t> out_bar, user_out_handle* out_handle) {
  if (dev_handle == ZX_HANDLE_INVALID || !out_bar || bar_num >= PCIE_MAX_BAR_REGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Grab the PCI device object
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(
      dev_handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK) {
    return status;
  }

  // Get bar info from the device via the dispatcher and make sure it makes sense
  const pcie_bar_info_t* info = pci_device->GetBar(bar_num);
  if (info == nullptr || info->size == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  // A bar can be MMIO, PIO, or unused. In the MMIO case it can be passed
  // back to the caller as a VMO.
  zx_pci_bar_t bar = {};
  bar.size = info->size;
  bar.type = (info->is_mmio) ? ZX_PCI_BAR_TYPE_MMIO : ZX_PCI_BAR_TYPE_PIO;

  // MMIO based bars are passed back using a VMO. If we end up creating one here
  // without errors then later a handle will be passed back to the caller.
  KernelHandle<VmObjectDispatcher> kernel_handle;
  fbl::RefPtr<VmObjectPhysical> vmo;
  zx_rights_t rights;
  if (info->is_mmio) {
    // Create a VMO mapping to the address / size of the mmio region this bar
    // was allocated at
    status =
        VmObjectPhysical::Create(info->bus_addr, ktl::max<uint64_t>(info->size, PAGE_SIZE), &vmo);
    if (status != ZX_OK) {
      return status;
    }

    // Set the name of the vmo for tracking
    char name[32];
    auto dev = pci_device->device();
    snprintf(name, sizeof(name), "pci-%02x:%02x.%1x-bar%u", dev->bus_id(), dev->dev_id(),
             dev->func_id(), bar_num);
    vmo->set_name(name, sizeof(name));

    // Now that the vmo has been created for the bar, create a handle to
    // the appropriate dispatcher for the caller
    status = VmObjectDispatcher::Create(vmo, &kernel_handle, &rights);
    if (status != ZX_OK) {
      return status;
    }

    pci_device->EnableMmio(true);
  } else {
    DEBUG_ASSERT(info->bus_addr != 0);
    bar.addr = info->bus_addr;
    pci_device->EnablePio(true);
  }

  // Metadata has been sorted out, so copy back the structure to userspace
  // and then account for the vmo handle if one was created.
  status = out_bar.copy_to_user(bar);
  if (status != ZX_OK) {
    return status;
  }

  if (vmo) {
    return out_handle->make(ktl::move(kernel_handle), rights);
  }

  return ZX_OK;
}

// zx_status_t zx_pci_map_interrupt
zx_status_t sys_pci_map_interrupt(zx_handle_t dev_handle, int32_t which_irq,
                                  user_out_handle* out_handle) {
  /**
   * Returns a handle that can be waited on.
   * @param handle Handle associated with a PCI device
   * @param which_irq Identifier for an IRQ, returned in sys_pci_get_nth_device
   * @param out_handle pointer to a handle to associate with the interrupt mapping
   */
  LTRACEF("handle %x\n", dev_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(dev_handle, ZX_RIGHT_READ, &pci_device);
  if (status != ZX_OK)
    return status;

  KernelHandle<InterruptDispatcher> interrupt_handle;
  zx_rights_t rights;
  zx_status_t result = pci_device->MapInterrupt(which_irq, &interrupt_handle, &rights);
  if (result != ZX_OK)
    return result;

  return out_handle->make(ktl::move(interrupt_handle), rights);
}

/**
 * Gets info about the capabilities of a PCI device's IRQ modes.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode whose capabilities are to be queried.
 * @param out_len Out param which will hold the maximum number of IRQs supported by the mode.
 */
// zx_status_t zx_pci_query_irq_mode
zx_status_t sys_pci_query_irq_mode(zx_handle_t dev_handle, uint32_t mode,
                                   user_out_ptr<uint32_t> out_max_irqs) {
  LTRACEF("handle %x\n", dev_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(dev_handle, ZX_RIGHT_READ, &pci_device);
  if (status != ZX_OK)
    return status;

  uint32_t max_irqs;
  zx_status_t result = pci_device->QueryIrqModeCaps((zx_pci_irq_mode_t)mode, &max_irqs);
  if (result != ZX_OK)
    return result;

  status = out_max_irqs.copy_to_user(max_irqs);
  if (status != ZX_OK)
    return status;

  return result;
}

/**
 * Selects an IRQ mode for a PCI device.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode to select.
 * @param requested_irq_count The number of IRQs to select request for the given mode.
 */
// zx_status_t zx_pci_set_irq_mode
zx_status_t sys_pci_set_irq_mode(zx_handle_t dev_handle, uint32_t mode,
                                 uint32_t requested_irq_count) {
  LTRACEF("handle %x\n", dev_handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PciDeviceDispatcher> pci_device;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
  if (status != ZX_OK)
    return status;

  return pci_device->SetIrqMode((zx_pci_irq_mode_t)mode, requested_irq_count);
}
#else  // WITH_KERNEL_PCIE
// zx_status_t zx_pci_init
zx_status_t sys_pci_init(zx_handle_t, user_in_ptr<const zx_pci_init_arg_t>, uint32_t) {
  shutdown_early_init_console();
  return ZX_OK;
}

// zx_status_t zx_pci_add_subtract_io_range
zx_status_t sys_pci_add_subtract_io_range(zx_handle_t handle, uint32_t mmio, uint64_t base,
                                          uint64_t len, uint32_t add) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_config_read
zx_status_t sys_pci_config_read(zx_handle_t handle, uint16_t offset, size_t width,
                                user_out_ptr<uint32_t> out_val) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_config_write
zx_status_t sys_pci_config_write(zx_handle_t handle, uint16_t offset, size_t width, uint32_t val) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_cfg_pio_rw
zx_status_t sys_pci_cfg_pio_rw(zx_handle_t handle, uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, user_inout_ptr<uint32_t> val, size_t width,
                               uint32_t write) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_get_nth_device
zx_status_t sys_pci_get_nth_device(zx_handle_t, uint32_t, user_inout_ptr<zx_pcie_device_info_t>,
                                   user_out_handle*) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_enable_bus_master
zx_status_t sys_pci_enable_bus_master(zx_handle_t, uint32_t) { return ZX_ERR_NOT_SUPPORTED; }

// zx_status_t zx_pci_reset_device
zx_status_t sys_pci_reset_device(zx_handle_t) { return ZX_ERR_NOT_SUPPORTED; }

// zx_status_t zx_pci_get_nth_device
zx_status_t sys_pci_get_nth_device(zx_handle_t hrsrc, uint32_t index,
                                   user_out_ptr<zx_pcie_device_info_t> out_info,
                                   user_out_handle* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_get_bar
zx_status_t sys_pci_get_bar(zx_handle_t dev_handle, uint32_t bar_num,
                            user_out_ptr<zx_pci_bar_t> out_bar, user_out_handle* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_map_interrupt
zx_status_t sys_pci_map_interrupt(zx_handle_t, int32_t, user_out_handle*) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_query_irq_mode
zx_status_t sys_pci_query_irq_mode(zx_handle_t, uint32_t, user_out_ptr<uint32_t>) {
  return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_pci_set_irq_mode
zx_status_t sys_pci_set_irq_mode(zx_handle_t, uint32_t, uint32_t) { return ZX_ERR_NOT_SUPPORTED; }
#endif  // WITH_KERNEL_PCIE
