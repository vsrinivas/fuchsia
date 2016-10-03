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
#include <dev/udisplay.h>
#include <kernel/vm.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/interrupt_dispatcher.h>
#include <magenta/interrupt_event_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls-types.h>
#include <magenta/user_copy.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

#if WITH_DEV_PCIE
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>

// Lookup routine for IRQ routing for PCIe
static uint32_t pcie_root_irq_map[PCIE_MAX_DEVICES_PER_BUS][PCIE_MAX_FUNCTIONS_PER_DEVICE][PCIE_MAX_LEGACY_IRQ_PINS];
static status_t pcie_irq_swizzle_from_table(uint bus_id,
                                            uint dev_id,
                                            uint func_id,
                                            uint pin,
                                            uint *irq)
{
    DEBUG_ASSERT(pin < 4);

    if (bus_id != 0) {
        return ERR_NOT_FOUND;
    }

    if ((bus_id  >= PCIE_MAX_BUSSES) ||
        (dev_id  >= PCIE_MAX_DEVICES_PER_BUS) ||
        (func_id >= PCIE_MAX_FUNCTIONS_PER_DEVICE)) {
        return ERR_INVALID_ARGS;
    }

    uint32_t val = pcie_root_irq_map[dev_id][func_id][pin];
    if (val == MX_PCI_NO_IRQ_MAPPING) {
        return ERR_NOT_FOUND;
    }

    *irq = val;
    return NO_ERROR;
}

mx_status_t sys_pci_init(mx_handle_t handle, user_ptr<mx_pci_init_arg_t> init_buf, uint32_t len) {

    // TODO: finer grained validation
    // TODO(security): Add additional access checks
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    mxtl::unique_ptr<mx_pci_init_arg_t, mxtl::free_delete> arg;

    if (len < sizeof(*arg) || len > MX_PCI_INIT_ARG_MAX_SIZE) {
        return ERR_INVALID_ARGS;
    }

    // we have to malloc instead of new since this is a variable-sized structure
    arg.reset(static_cast<mx_pci_init_arg_t*>(malloc(len)));
    if (!arg) {
        return ERR_NO_MEMORY;
    }
    {
        mx_status_t status = init_buf.reinterpret<const void>().copy_array_from_user(arg.get(), len);
        if (status != NO_ERROR) {
            return status;
        }
    }

    const uint32_t win_count = arg->ecam_window_count;
    if (len != sizeof(*arg) + sizeof(arg->ecam_windows[0]) * win_count) {
        return ERR_INVALID_ARGS;
    }

    if (arg->num_irqs > countof(arg->irqs)) {
        return ERR_INVALID_ARGS;
    }

    // Configure interrupts
    for (unsigned int i = 0; i < arg->num_irqs; ++i) {
        uint32_t irq = arg->irqs[i].global_irq;
        enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
        if (arg->irqs[i].level_triggered) {
            tm = IRQ_TRIGGER_MODE_LEVEL;
        }
        enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
        if (arg->irqs[i].active_high) {
            pol = IRQ_POLARITY_ACTIVE_HIGH;
        }

        status_t status = configure_interrupt(irq, tm, pol);
        if (status != NO_ERROR) {
            return status;
        }
    }

    // TODO(teisenbe): For now assume there is only one ECAM
    if (win_count != 1) {
        return ERR_INVALID_ARGS;
    }
    if (arg->ecam_windows[0].bus_start != 0) {
        return ERR_INVALID_ARGS;
    }

    static_assert(sizeof(pcie_root_irq_map) == sizeof(arg->dev_pin_to_global_irq));
    memcpy(&pcie_root_irq_map, &arg->dev_pin_to_global_irq, sizeof(pcie_root_irq_map));

    if (arg->ecam_windows[0].bus_start > arg->ecam_windows[0].bus_end) {
        return ERR_INVALID_ARGS;
    }

#if ARCH_X86
    // Check for a quirk that we've seen.  Some systems will report overly large
    // PCIe config regions that collide with architectural registers.
    unsigned int num_buses = arg->ecam_windows[0].bus_end -
            arg->ecam_windows[0].bus_start + 1;
    paddr_t end = arg->ecam_windows[0].base +
            num_buses * PCIE_ECAM_BYTE_PER_BUS;
    const paddr_t high_limit = 0xfec00000ULL;
    if (end > high_limit) {
        TRACEF("PCIe config space collides with arch devices, truncating\n");
        end = high_limit;
        if (end < arg->ecam_windows[0].base) {
            return ERR_INVALID_ARGS;
        }
        arg->ecam_windows[0].size = ROUNDDOWN(end - arg->ecam_windows[0].base,
                                              PCIE_ECAM_BYTE_PER_BUS);
        uint64_t new_bus_end = (arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) +
                arg->ecam_windows[0].bus_start - 1;
        if (new_bus_end >= PCIE_MAX_BUSSES) {
            return ERR_INVALID_ARGS;
        }
        arg->ecam_windows[0].bus_end = static_cast<uint8_t>(new_bus_end);
    }
#endif

    if (arg->ecam_windows[0].size < PCIE_ECAM_BYTE_PER_BUS) {
        return ERR_INVALID_ARGS;
    }
    if (arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS >
        PCIE_MAX_BUSSES - arg->ecam_windows[0].bus_start) {

        return ERR_INVALID_ARGS;
    }

    // TODO(johngro): Do not limit this to a single range.  Instead, fetch all
    // of the ECAM ranges from ACPI, as well as the appropriate bus start/end
    // ranges.
    const pcie_ecam_range_t ecam_windows[] = {
        {
            .io_range  = {
                .bus_addr = arg->ecam_windows[0].base,
                .size = arg->ecam_windows[0].size
            },
            .bus_start = 0x00,
            .bus_end   = static_cast<uint8_t>((arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) - 1),
        },
    };

    pcie_init_info_t init_info;
    platform_pcie_init_info(&init_info);

    // Only override fields if they're NULL
    if (!init_info.ecam_windows) {
        init_info.ecam_windows = ecam_windows;
        init_info.ecam_window_count = countof(ecam_windows);
    }
    if (!init_info.legacy_irq_swizzle) {
        init_info.legacy_irq_swizzle = pcie_irq_swizzle_from_table;
    }

    return pcie_init(&init_info);
}

mx_handle_t sys_pci_get_nth_device(mx_handle_t hrsrc, uint32_t index, mx_pcie_get_nth_info_t* out_info) {
    /**
     * Returns the pci config of a device.
     * @param index Device index
     * @param out_info Device info (BDF address, vendor id, etc...)
     */
    LTRACE_ENTRY;

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(hrsrc)) < 0) {
        return status;
    }

    if (!out_info)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_pcie_get_nth_info_t info;
    status_t result = PciDeviceDispatcher::Create(index, &info, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t handle_value = up->MapHandleToValue(handle.get());

    if (copy_to_user_unsafe(reinterpret_cast<uint8_t*>(out_info),
                            &info, sizeof(*out_info)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return handle_value;
}

mx_status_t sys_pci_claim_device(mx_handle_t handle) {
    /**
     * Claims the PCI device associated with the handle. Called when a driver
     * successfully probes the device.
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->ClaimDevice();
}

mx_status_t sys_pci_enable_bus_master(mx_handle_t handle, bool enable) {
    /**
     * Enables or disables bus mastering for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param enable true if bus mastering should be enabled.
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->EnableBusMaster(enable);
}

mx_status_t sys_pci_reset_device(mx_handle_t handle) {
    /**
     * Resets the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->ResetDevice();
}

mx_handle_t sys_pci_map_mmio(mx_handle_t handle, uint32_t bar_num, mx_cache_policy_t cache_policy) {
    /**
     * Performs MMIO mapping for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     */
    LTRACEF("handle %d\n", handle);

    // Caller only gets to control the cache policy, nothing else.
    if (cache_policy & ~ARCH_MMU_FLAG_CACHE_MASK)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    mx_rights_t mmio_rights;
    mxtl::RefPtr<Dispatcher> mmio_io_mapping;
    status_t result = pci_device->MapMmio(bar_num, cache_policy, &mmio_io_mapping, &mmio_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr mmio_handle(MakeHandle(mxtl::move(mmio_io_mapping), mmio_rights));
    if (!handle)
        return ERR_NO_MEMORY;

    mx_handle_t ret_val = up->MapHandleToValue(mmio_handle.get());
    up->AddHandle(mxtl::move(mmio_handle));
    return ret_val;
}

mx_status_t sys_pci_io_write(mx_handle_t handle, uint32_t bar_num, uint32_t offset, uint32_t len,
                             uint32_t value) {
    /**
     * Performs port I/O write for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     * @param offset Offset from the base
     * @param len Length of the operation in bytes
     * @param value_ptr Pointer to the value to write
     */
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_read(mx_handle_t handle, uint32_t bar_num, uint32_t offset, uint32_t len,
                            uint32_t* out_value_ptr) {
    /**
     * Performs port I/O read for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     * @param offset Offset from the base
     * @param len Length of the operation in bytes
     * @param out_value_ptr Pointer to read the value into
     */
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_interrupt(mx_handle_t handle_value, int32_t which_irq) {
    /**
     * Returns a handle that can be waited on.
     * @param handle Handle associated with a PCI device
     * @param which_irq Identifier for an IRQ, returned in sys_pci_get_nth_device, or -1 for legacy
     * interrupts
     */
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status =
        up->GetDispatcher(handle_value, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> interrupt_dispatcher;
    mx_rights_t rights;
    status_t result = pci_device->MapInterrupt(which_irq, &interrupt_dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(interrupt_dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    mx_handle_t interrupt_handle = up->MapHandleToValue(handle.get());
    up->AddHandle(mxtl::move(handle));
    return interrupt_handle;
}

mx_handle_t sys_pci_map_config(mx_handle_t handle) {
    /**
     * Fetch an I/O Mapping object which maps the PCI device's mmaped config
     * into the caller's address space (read only)
     *
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    mx_rights_t config_rights;
    mxtl::RefPtr<Dispatcher> config_io_mapping;
    status_t result = pci_device->MapConfig(&config_io_mapping, &config_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr config_handle(MakeHandle(mxtl::move(config_io_mapping), config_rights));
    if (!config_handle)
        return ERR_NO_MEMORY;

    mx_handle_t ret_val = up->MapHandleToValue(config_handle.get());
    up->AddHandle(mxtl::move(config_handle));
    return ret_val;
}

/**
 * Gets info about the capabilities of a PCI device's IRQ modes.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode whose capabilities are to be queried.
 * @param out_len Out param which will hold the maximum number of IRQs supported by the mode.
 */
mx_status_t sys_pci_query_irq_mode_caps(mx_handle_t handle,
                                        mx_pci_irq_mode_t mode,
                                        uint32_t* out_max_irqs) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    uint32_t max_irqs;
    status_t result = pci_device->QueryIrqModeCaps(mode, &max_irqs);
    if (result != NO_ERROR)
        return result;

    if (copy_to_user_unsafe(reinterpret_cast<uint8_t*>(out_max_irqs),
                            &max_irqs, sizeof(*out_max_irqs)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return result;
}

/**
 * Selects an IRQ mode for a PCI device.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode to select.
 * @param requested_irq_count The number of IRQs to select request for the given mode.
 */
mx_status_t sys_pci_set_irq_mode(mx_handle_t handle,
                                 mx_pci_irq_mode_t mode,
                                 uint32_t requested_irq_count) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->SetIrqMode(mode, requested_irq_count);
}
#else  // WITH_DEV_PCIE
mx_status_t sys_pci_init(mx_handle_t, user_ptr<mx_pci_init_arg_t>, uint32_t) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_get_nth_device(mx_handle_t, uint32_t, mx_pcie_get_nth_info_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_claim_device(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_enable_bus_master(mx_handle_t, bool) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_reset_device(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_mmio(mx_handle_t, uint32_t, mx_cache_policy_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_write(mx_handle_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_read(mx_handle_t, uint32_t, uint32_t, uint32_t, uint32_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_interrupt(mx_handle_t, int32_t) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_config(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_query_irq_mode_caps(mx_handle_t, mx_pci_irq_mode_t, uint32_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_set_irq_mode(mx_handle_t, mx_pci_irq_mode_t, uint32_t) {
    return ERR_NOT_SUPPORTED;
}
#endif  // WITH_DEV_PCIE
