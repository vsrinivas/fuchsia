// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <lib/pci/pio.h>
#include <lib/user_copy/user_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_physical.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/limits.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_free_ptr.h>
#include <zircon/syscalls/pci.h>

#include "priv.h"

#define LOCAL_TRACE 0

#if WITH_LIB_GFXCONSOLE
// If we were built with the GFX console, make sure that it is un-bound when
// user mode takes control of PCI.  Note: there should probably be a cleaner way
// of doing this.  Not all system have PCI, and (eventually) not all systems
// will attempt to initialize PCI.  Someday, there should be a different way of
// handing off from early/BSOD kernel mode graphics to user mode.
#include <lib/gfxconsole.h>
static inline void shutdown_early_init_console() {
    gfxconsole_bind_display(nullptr, nullptr);
}
#else
static inline void shutdown_early_init_console() {}
#endif

#if WITH_DEV_PCIE
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_root.h>
#include <object/pci_device_dispatcher.h>

// Implementation of a PcieRoot with a look-up table based legacy IRQ swizzler
// suitable for use with ACPI style swizzle definitions.
class PcieRootLUTSwizzle : public PcieRoot {
public:
    static fbl::RefPtr<PcieRoot> Create(PcieBusDriver& bus_drv,
                                        uint managed_bus_id,
                                        const zx_pci_irq_swizzle_lut_t& lut) {
        fbl::AllocChecker ac;
        auto root = fbl::AdoptRef(new (&ac) PcieRootLUTSwizzle(bus_drv,
                                                               managed_bus_id,
                                                               lut));
        if (!ac.check()) {
            TRACEF("Out of memory attemping to create PCIe root to manage bus ID 0x%02x\n",
                   managed_bus_id);
            return nullptr;
        }

        return root;
    }

    zx_status_t Swizzle(uint dev_id, uint func_id, uint pin, uint* irq) override {
        if ((irq == nullptr) ||
            (dev_id >= fbl::count_of(lut_)) ||
            (func_id >= fbl::count_of(lut_[dev_id])) ||
            (pin >= fbl::count_of(lut_[dev_id][func_id])))
            return ZX_ERR_INVALID_ARGS;

        *irq = lut_[dev_id][func_id][pin];
        return (*irq == ZX_PCI_NO_IRQ_MAPPING) ? ZX_ERR_NOT_FOUND : ZX_OK;
    }

private:
    PcieRootLUTSwizzle(PcieBusDriver& bus_drv,
                       uint managed_bus_id,
                       const zx_pci_irq_swizzle_lut_t& lut)
        : PcieRoot(bus_drv, managed_bus_id) {
        ::memcpy(&lut_, &lut, sizeof(lut_));
    }

    zx_pci_irq_swizzle_lut_t lut_;
};

// Scan |lut| for entries mapping to |irq|, and replace them with
// ZX_PCI_NO_IRQ_MAPPING.
static void pci_irq_swizzle_lut_remove_irq(zx_pci_irq_swizzle_lut_t* lut, uint32_t irq) {
    for (size_t dev = 0; dev < fbl::count_of(*lut); ++dev) {
        for (size_t func = 0; func < fbl::count_of((*lut)[dev]); ++func) {
            for (size_t pin = 0; pin < fbl::count_of((*lut)[dev][func]); ++pin) {
                uint32_t* assigned_irq = &(*lut)[dev][func][pin];
                if (*assigned_irq == irq) {
                    *assigned_irq = ZX_PCI_NO_IRQ_MAPPING;
                }
            }
        }
    }
}

zx_status_t sys_pci_add_subtract_io_range(zx_handle_t handle, bool mmio, uint64_t base, uint64_t len, bool add) {

    LTRACEF("handle %x mmio %d base %#" PRIx64 " len %#" PRIx64 " add %d\n", handle, mmio, base, len, add);

    // TODO(ZX-971): finer grained validation
    // TODO(security): Add additional access checks
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    auto pcie = PcieBusDriver::GetDriver();
    if (pcie == nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    PciAddrSpace addr_space = mmio ? PciAddrSpace::MMIO : PciAddrSpace::PIO;

    if (add) {
        return pcie->AddBusRegion(base, len, addr_space);
    } else {
        return pcie->SubtractBusRegion(base, len, addr_space);
    }
}

zx_status_t sys_pci_init(zx_handle_t handle, user_in_ptr<const zx_pci_init_arg_t> _init_buf, uint32_t len) {
    // TODO(ZX-971): finer grained validation
    // TODO(security): Add additional access checks
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    fbl::unique_free_ptr<zx_pci_init_arg_t> arg;

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
    {
        status = _init_buf.reinterpret<const void>().copy_array_from_user(
            arg.get(), len);
        if (status != ZX_OK) {
            return status;
        }
    }

    if (LOCAL_TRACE) {
        TRACEF("%u address window%s found in init arg\n", arg->addr_window_count,
               (arg->addr_window_count == 1) ? "" : "s");
        for (uint32_t i = 0; i < arg->addr_window_count; i++) {
            printf("[%u]\n\tis_mmio: %d\n\thas_ecam: %d\n\tbase: %#" PRIxPTR "\n"
                   "\tsize: %zu\n\tbus_start: %u\n\tbus_end: %u\n",
                   i,
                   arg->addr_windows[i].is_mmio, arg->addr_windows[i].has_ecam,
                   arg->addr_windows[i].base, arg->addr_windows[i].size,
                   arg->addr_windows[i].bus_start, arg->addr_windows[i].bus_end);
        }
    }

    const uint32_t win_count = arg->addr_window_count;
    if (len != sizeof(*arg) + sizeof(arg->addr_windows[0]) * win_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (arg->num_irqs > fbl::count_of(arg->irqs)) {
        return ZX_ERR_INVALID_ARGS;
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
    // TODO(teisenbe): For now assume there is only one ECAM
    if (win_count != 1) {
        return ZX_ERR_INVALID_ARGS;
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
    unsigned int num_buses = arg->addr_windows[0].bus_end -
                             arg->addr_windows[0].bus_start + 1;
    paddr_t end = arg->addr_windows[0].base +
                  num_buses * PCIE_ECAM_BYTE_PER_BUS;
    const paddr_t high_limit = 0xfec00000ULL;
    if (end > high_limit) {
        TRACEF("PCIe config space collides with arch devices, truncating\n");
        end = high_limit;
        if (end < arg->addr_windows[0].base) {
            return ZX_ERR_INVALID_ARGS;
        }
        arg->addr_windows[0].size = ROUNDDOWN(end - arg->addr_windows[0].base,
                                              PCIE_ECAM_BYTE_PER_BUS);
        uint64_t new_bus_end = (arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) +
                               arg->addr_windows[0].bus_start - 1;
        if (new_bus_end >= PCIE_MAX_BUSSES) {
            return ZX_ERR_INVALID_ARGS;
        }
        arg->addr_windows[0].bus_end = static_cast<uint8_t>(new_bus_end);
    }
#endif

    if (arg->addr_windows[0].is_mmio) {
        if (arg->addr_windows[0].size < PCIE_ECAM_BYTE_PER_BUS) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS >
            PCIE_MAX_BUSSES - arg->addr_windows[0].bus_start) {

            return ZX_ERR_INVALID_ARGS;
        }

        // TODO(johngro): Update the syscall to pass a paddr_t for base instead of a uint64_t
        ASSERT(arg->addr_windows[0].base < fbl::numeric_limits<paddr_t>::max());

        // TODO(johngro): Do not limit this to a single range.  Instead, fetch all
        // of the ECAM ranges from ACPI, as well as the appropriate bus start/end
        // ranges.
        const PcieBusDriver::EcamRegion ecam = {
            .phys_base = static_cast<paddr_t>(arg->addr_windows[0].base),
            .size = arg->addr_windows[0].size,
            .bus_start = 0x00,
            .bus_end = static_cast<uint8_t>((arg->addr_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) - 1),
        };

        zx_status_t ret = pcie->AddEcamRegion(ecam);
        if (ret != ZX_OK) {
            TRACEF("Failed to add ECAM region to PCIe bus driver! (ret %d)\n", ret);
            return ret;
        }
    }

    // TODO(johngro): Change the user-mode and devmgr behavior to add all of the
    // roots in the system.  Do not assume that there is a single root, nor that
    // it manages bus ID 0.
    auto root = PcieRootLUTSwizzle::Create(*pcie, 0, arg->dev_pin_to_global_irq);
    if (root == nullptr)
        return ZX_ERR_NO_MEMORY;

    // Enable PIO config space if the address window was not MMIO
    pcie->EnablePIOWorkaround(!arg->addr_windows[0].is_mmio);

    zx_status_t ret = pcie->AddRoot(fbl::move(root));
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

zx_status_t sys_pci_get_nth_device(zx_handle_t hrsrc,
                                   uint32_t index,
                                   user_out_ptr<zx_pcie_device_info_t> out_info,
                                   user_out_handle* out_handle) {
    /**
     * Returns the pci config of a device.
     * @param index Device index
     * @param out_info Device info (BDF address, vendor id, etc...)
     */
    LTRACEF("handle %x index %u\n", hrsrc, index);

    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    if (!out_info) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_pcie_device_info_t info;
    status = PciDeviceDispatcher::Create(index, &info, &dispatcher, &rights);
    if (status != ZX_OK) {
        return status;
    }

    // If everything is successful add the handle to the process
    status = out_info.copy_to_user(info);
    if (status != ZX_OK)
        return status;

    return out_handle->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_pci_config_read(zx_handle_t handle, uint16_t offset, size_t width,
                                user_out_ptr<uint32_t> out_val) {
    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    fbl::RefPtr<Dispatcher> dispatcher;

    // Get the PciDeviceDispatcher from the handle passed in via the pci protocol
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                                                     &pci_device);
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

zx_status_t sys_pci_config_write(zx_handle_t handle, uint16_t offset, size_t width, uint32_t val) {
    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    fbl::RefPtr<Dispatcher> dispatcher;

    // Get the PciDeviceDispatcher from the handle passed in via the pci protocol
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ | ZX_RIGHT_WRITE,
                                                     &pci_device);
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
zx_status_t sys_pci_cfg_pio_rw(zx_handle_t handle, uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, user_inout_ptr<uint32_t> val, size_t width, bool write) {
#if ARCH_X86
    uint32_t val_;
    zx_status_t status = validate_resource(handle, ZX_RSRC_KIND_ROOT);
    if (status != ZX_OK) {
        return status;
    }

    if (write) {
        val.copy_from_user(&val_);
        status = Pci::PioCfgWrite(bus, dev, func, offset, val_, width);
    } else {
        status = Pci::PioCfgRead(bus, dev, func, offset, &val_, width);
        if (status == ZX_OK) {
            val.copy_to_user(val_);
        }
    }

    return status;
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
}

zx_status_t sys_pci_enable_bus_master(zx_handle_t dev_handle, bool enable) {
    /**
     * Enables or disables bus mastering for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param enable true if bus mastering should be enabled.
     */
    LTRACEF("handle %x\n", dev_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status = up->GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
    if (status != ZX_OK)
        return status;

    return pci_device->EnableBusMaster(enable);
}

zx_status_t sys_pci_reset_device(zx_handle_t dev_handle) {
    /**
     * Resets the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %x\n", dev_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status = up->GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
    if (status != ZX_OK)
        return status;

    return pci_device->ResetDevice();
}

zx_status_t sys_pci_get_bar(zx_handle_t dev_handle,
                            uint32_t bar_num,
                            user_out_ptr<zx_pci_bar_t> out_bar,
                            user_out_handle* out_handle) {
    if (dev_handle == ZX_HANDLE_INVALID ||
        !out_bar ||
        bar_num >= PCIE_MAX_BAR_REGS) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Grab the PCI device object
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status = up->GetDispatcherWithRights(dev_handle,
                                                     ZX_RIGHT_READ | ZX_RIGHT_WRITE, &pci_device);
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
    bar.type = (info->is_mmio) ? PCI_BAR_TYPE_MMIO : PCI_BAR_TYPE_PIO;

    // MMIO based bars are passed back using a VMO. If we end up creating one here
    // without errors then later a handle will be passed back to the caller.
    fbl::RefPtr<Dispatcher> dispatcher;
    fbl::RefPtr<VmObject> vmo;
    zx_rights_t rights;
    if (info->is_mmio) {
        // Create a VMO mapping to the address / size of the mmio region this bar
        // was allocated at
        status = VmObjectPhysical::Create(info->bus_addr,
                                          fbl::max<uint64_t>(info->size, PAGE_SIZE), &vmo);
        if (status != ZX_OK) {
            return status;
        }

        // Set the name of the vmo for tracking
        char name[32];
        auto dev = pci_device->device();
        snprintf(name, sizeof(name), "pci-%02x:%02x.%1x-bar%u",
                 dev->bus_id(), dev->dev_id(), dev->func_id(), bar_num);
        vmo->set_name(name, sizeof(name));

        // Now that the vmo has been created for the bar, create a handle to
        // the appropriate dispatcher for the caller
        status = VmObjectDispatcher::Create(vmo, &dispatcher, &rights);
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
        return out_handle->make(fbl::move(dispatcher), rights);
    }

    return ZX_OK;
}

zx_status_t sys_pci_map_interrupt(zx_handle_t dev_handle,
                                  int32_t which_irq,
                                  user_out_handle* out_handle) {
    /**
     * Returns a handle that can be waited on.
     * @param handle Handle associated with a PCI device
     * @param which_irq Identifier for an IRQ, returned in sys_pci_get_nth_device, or -1 for legacy
     * interrupts
     * @param out_handle pointer to a handle to associate with the interrupt mapping
     */
    LTRACEF("handle %x\n", dev_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status =
        up->GetDispatcherWithRights(dev_handle, ZX_RIGHT_READ, &pci_device);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<Dispatcher> interrupt_dispatcher;
    zx_rights_t rights;
    zx_status_t result = pci_device->MapInterrupt(which_irq, &interrupt_dispatcher, &rights);
    if (result != ZX_OK)
        return result;

    return out_handle->make(fbl::move(interrupt_dispatcher), rights);
}

/**
 * Gets info about the capabilities of a PCI device's IRQ modes.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode whose capabilities are to be queried.
 * @param out_len Out param which will hold the maximum number of IRQs supported by the mode.
 */
zx_status_t sys_pci_query_irq_mode(zx_handle_t dev_handle,
                                   uint32_t mode,
                                   user_out_ptr<uint32_t> out_max_irqs) {
    LTRACEF("handle %x\n", dev_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status = up->GetDispatcherWithRights(dev_handle, ZX_RIGHT_READ, &pci_device);
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
zx_status_t sys_pci_set_irq_mode(zx_handle_t dev_handle,
                                 uint32_t mode,
                                 uint32_t requested_irq_count) {
    LTRACEF("handle %x\n", dev_handle);

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<PciDeviceDispatcher> pci_device;
    zx_status_t status = up->GetDispatcherWithRights(dev_handle, ZX_RIGHT_WRITE, &pci_device);
    if (status != ZX_OK)
        return status;

    return pci_device->SetIrqMode((zx_pci_irq_mode_t)mode, requested_irq_count);
}
#else  // WITH_DEV_PCIE
zx_status_t sys_pci_init(zx_handle_t, user_in_ptr<const zx_pci_init_arg_t>, uint32_t) {
    shutdown_early_init_console();
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_add_subtract_io_range(zx_handle_t handle, bool mmio, uint64_t base, uint64_t len, bool add) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_config_read(zx_handle_t handle, uint16_t offset, size_t width,
                                user_out_ptr<uint32_t> out_val) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_config_write(zx_handle_t handle, uint16_t offset, size_t width, uint32_t val) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_cfg_pio_rw(zx_handle_t handle, uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, user_inout_ptr<uint32_t> val, size_t width, bool write) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_get_nth_device(zx_handle_t, uint32_t, user_inout_ptr<zx_pcie_device_info_t>,
                                   user_out_handle*) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_enable_bus_master(zx_handle_t, bool) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_reset_device(zx_handle_t) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_get_bar(zx_handle_t, uint32_t, pci_resource_t**, user_out_handle*) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_map_interrupt(zx_handle_t, int32_t, user_out_handle*) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_query_irq_mode(zx_handle_t, uint32_t, user_out_ptr<uint32_t>) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_pci_set_irq_mode(zx_handle_t, uint32_t, uint32_t) {
    return ZX_ERR_NOT_SUPPORTED;
}
#endif // WITH_DEV_PCIE
