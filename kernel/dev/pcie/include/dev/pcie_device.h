// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/errors.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_caps.h>
#include <dev/pcie_constants.h>
#include <dev/pcie_irqs.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <mxtl/macros.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <sys/types.h>

/* Fwd decls */
class PcieBusDriver;
struct pcie_bridge_state_t;
struct pcie_config_t;

/*
 * struct used to fetch information about a configured base address register
 */
struct pcie_bar_info_t {
    uint64_t size = 0;
    uint64_t bus_addr = 0;
    bool     is_mmio;
    bool     is_64bit;
    bool     is_prefetchable;
    uint     first_bar_reg;
    RegionAllocator::Region::UPtr allocation;
};

/*
 * Struct used to manage the relationship between a PCIe device/function and its
 * associated driver.  During a bus scan/probe operation, all drivers will have
 * their registered probe methods called until a driver claims a device.  A
 * driver may claim a device by returning a pointer to a driver-managed
 * pcie_device_state struct, with the driver owned fields filled out.
 */
struct pcie_device_state_t : public mxtl::RefCounted<pcie_device_state_t> {
    pcie_device_state_t(PcieBusDriver& bus_driver);
    virtual ~pcie_device_state_t();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(pcie_device_state_t);

    virtual void Unplug();
    mxtl::RefPtr<pcie_bridge_state_t> GetUpstream() { return bus_drv.GetUpstream(*this); }
    mxtl::RefPtr<pcie_bridge_state_t> DowncastToBridge();

    PcieBusDriver&                    bus_drv;   // Reference to our bus driver state.
    pcie_config_t*                    cfg;       // Pointer to the memory mapped ECAM (kernel vaddr)
    paddr_t                           cfg_phys;  // The physical address of the device's ECAM
    mxtl::RefPtr<pcie_bridge_state_t> upstream;  // The upstream bridge, or NULL if we are root
    bool                              is_bridge; // True if this device is also a bridge
    uint16_t                          vendor_id; // The device's vendor ID, as read from config
    uint16_t                          device_id; // The device's device ID, as read from config
    uint8_t                           class_id;  // The device's class ID, as read from config.
    uint8_t                           subclass;  // The device's subclass, as read from config.
    uint8_t                           prog_if;   // The device's programming interface (from cfg)
    uint                              bus_id;    // The bus ID this bridge/device exists on
    uint                              dev_id;    // The device ID of this bridge/device
    uint                              func_id;   // The function ID of this bridge/device
    SpinLock                          cmd_reg_lock;

    /* State related to lifetime management */
    mutable Mutex                     dev_lock;
    bool                              plugged_in;
    bool                              disabled;
    bool                              claimed;

    /* Info about the BARs computed and cached during the initial setup/probe,
     * indexed by starting BAR register index */
    pcie_bar_info_t bars[PCIE_MAX_BAR_REGS];
    uint bar_count;

    /* PCI Express Capabilities (Standard Capability 0x10) if present */
    struct {
        uint               version;  // version of the caps structure.
        pcie_device_type_t devtype;  // device type parts from pcie_caps
        bool               has_flr;  // true if device supports function level reset
        pcie_caps_hdr_t*   ecam;     // pointer to the caps structure header in ECAM

        /* Pointers to various chunk structures which may or may not be present
         * in the caps structure.  All of these chunks will be present in a v2
         * structure, but only some of the chunks may be present (depending on
         * device type) in a v1 structure. */
        pcie_caps_chunk_t*      chunks[PCS_CAPS_CHUNK_COUNT];
        pcie_caps_root_chunk_t* root;
    } pcie_caps;

    /* PCI Advanced Capabilities (Standard Capability 0x13) if present */
    struct {
        pcie_cap_adv_caps_t* ecam;     // pointer to the adv caps structure in ECAM
        bool                 has_flr;  // true if device supports function level reset
    } pcie_adv_caps;

    /* IRQ configuration and handling state */
    struct {
        /* Shared state */
        pcie_irq_mode_t           mode = PCIE_IRQ_MODE_DISABLED;
        pcie_irq_handler_state_t  singleton_handler;
        pcie_irq_handler_state_t* handlers = nullptr;
        uint                      handler_count = 0;
        uint                      registered_handler_count = 0;

        /* Legacy IRQ state */
        struct {
            // TODO(johngro): clean up the messy list_node initialization below
            // by converting to mxtl intrusive lists.
            uint8_t pin = 0;
            struct list_node shared_handler_node = { nullptr, nullptr};
            mxtl::RefPtr<SharedLegacyIrqHandler> shared_handler;
        } legacy;

        /* MSI state */
        struct {
            pcie_cap_msi_t*    cfg = nullptr;
            uint               max_irqs = 0;
            bool               is64bit;
            volatile uint32_t* pvm_mask_reg = nullptr;
            pcie_msi_block_t   irq_block;
        } msi;

        /* TODO(johngro) : Add MSI-X state */
        struct { } msi_x;
    } irq;

};

/*
 * Attaches a driver to a PCI device. Returns ERR_ALREADY_BOUND if the device has already been
 * claimed by another driver.
 */
status_t pcie_claim_device(const mxtl::RefPtr<pcie_device_state_t>& device);

/*
 * Unclaim a device had been successfully claimed with pcie_claim_device().
 */
void pcie_unclaim_device(const mxtl::RefPtr<pcie_device_state_t>& device);

/*
 * Trigger a function level reset (if possible)
 */
status_t pcie_do_function_level_reset(const mxtl::RefPtr<pcie_device_state_t>& dev);

/*
 * temporary hack.
 * Previously static functions which are about to migrate into the device class.
 */
status_t pcie_allocate_bars(const mxtl::RefPtr<pcie_device_state_t>& dev);
status_t pcie_scan_init_device(const mxtl::RefPtr<pcie_device_state_t>& dev,
                               const mxtl::RefPtr<pcie_bridge_state_t>& upstream_bridge,
                               uint                                     bus_id,
                               uint                                     dev_id,
                               uint                                     func_id);

/*
 * Return information about the requested base address register, if it has been
 * allocated.  Otherwise, return NULL.
 *
 * @param dev A pointer to the pcie device/bridge node to fetch BAR info for.
 * @param bar_ndx The index of the BAR register to fetch info for.
 *
 * @return A pointer to the BAR info, including where in the bus address space
 * the BAR window has been mapped, or NULL if the BAR window does not exist or
 * has not been allocated.
 */
static inline const pcie_bar_info_t* pcie_get_bar_info(
        const pcie_device_state_t& dev,
        uint bar_ndx) {
    DEBUG_ASSERT(bar_ndx < countof(dev.bars));
    const pcie_bar_info_t* ret = &dev.bars[bar_ndx];
    return (!dev.disabled && (ret->allocation != nullptr)) ? ret : NULL;
}

/*
 * Modify bits in the device's command register (in the device config space),
 * clearing the bits specified by clr_bits and setting the bits specified by set
 * bits.  Specifically, the operation will be applied as...
 *
 * WR(cmd, (RD(cmd) & ~clr) | set)
 *
 * @param device A pointer to the device whose command register is to be
 * modified.
 * @param clr_bits The mask of bits to be cleared.
 * @param clr_bits The mask of bits to be set.
 * @return A status_t indicating success or failure of the operation.
 */
status_t pcie_modify_cmd(const mxtl::RefPtr<pcie_device_state_t>& device,
                         uint16_t clr_bits, uint16_t set_bits);

/*
 * Enable or disable bus mastering in a device's configuration.
 *
 * @param device A pointer to the target device.
 * @param enable If true, allow the device to access main system memory as a bus
 * master.
 * @return A status_t indicating success or failure of the operation.
 */
static inline status_t pcie_enable_bus_master(const mxtl::RefPtr<pcie_device_state_t>& device,
                                              bool enabled) {
    if (enabled && device->disabled)
        return ERR_BAD_STATE;

    return pcie_modify_cmd(device,
                           enabled ? 0 : PCI_COMMAND_BUS_MASTER_EN,
                           enabled ? PCI_COMMAND_BUS_MASTER_EN : 0);
}

/*
 * Enable or disable PIO access in a device's configuration.
 *
 * @param device A pointer to the target device.
 * @param enable If true, allow the device to access its PIO mapped registers.
 * @return A status_t indicating success or failure of the operation.
 */
static inline status_t pcie_enable_pio(const mxtl::RefPtr<pcie_device_state_t>& device,
                                       bool enabled) {
    if (enabled && device->disabled)
        return ERR_BAD_STATE;

    return pcie_modify_cmd(device,
                           enabled ? 0 : PCI_COMMAND_IO_EN,
                           enabled ? PCI_COMMAND_IO_EN : 0);
}

/*
 * Enable or disable MMIO access in a device's configuration.
 *
 * @param device A pointer to the target device.
 * @param enable If true, allow the device to access its MMIO mapped registers.
 * @return A status_t indicating success or failure of the operation.
 */
static inline status_t pcie_enable_mmio(const mxtl::RefPtr<pcie_device_state_t>& device,
                                        bool enabled) {
    if (enabled && device->disabled)
        return ERR_BAD_STATE;

    return pcie_modify_cmd(device,
                           enabled ? 0 : PCI_COMMAND_MEM_EN,
                           enabled ? PCI_COMMAND_MEM_EN : 0);
}
