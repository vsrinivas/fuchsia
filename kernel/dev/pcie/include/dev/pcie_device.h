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
#include <dev/pci_common.h>
#include <dev/pcie_irqs.h>
#include <dev/pcie_ref_counted.h>
#include <dev/pci_config.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm/vm_object.h>
#include <mxtl/macros.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>
#include <sys/types.h>

/* Fwd decls */
class  PcieBusDriver;
class  PcieUpstreamNode;

/*
 * struct used to fetch information about a config
 */
struct pci_config_info_t {
    uint64_t size = 0;
    uint64_t base_addr = 0;
    bool     is_mmio;
    mxtl::RefPtr<VmObject> vmo;
};

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
    mxtl::RefPtr<VmObject> vmo;
    RegionAllocator::Region::UPtr allocation;
};

/*
 * Base used to manage the relationship between a PCIe device/function and its
 * associated driver.  During a bus scan/probe operation, all drivers will have
 * their registered probe methods called until a driver claims a device.  A
 * driver may claim a device by returning a pointer to a driver-managed
 * pcie_device_state struct, with the driver owned fields filled out.
 */
class PcieDevice {
public:
    using CapabilityList = mxtl::SinglyLinkedList<mxtl::unique_ptr<PciStdCapability>>;
    static mxtl::RefPtr<PcieDevice> Create(PcieUpstreamNode& upstream, uint dev_id, uint func_id);
    virtual ~PcieDevice();

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieDevice);

    // Require that derived classes implement ref counting.
    PCIE_REQUIRE_REFCOUNTED;

    mxtl::RefPtr<PcieUpstreamNode> GetUpstream();

    status_t     Claim();
    void         Unclaim();
    virtual void Unplug();

    /*
     * Trigger a function level reset (if possible)
     */
    status_t DoFunctionLevelReset();

    /*
     * Modify bits in the device's command register (in the device config space),
     * clearing the bits specified by clr_bits and setting the bits specified by set
     * bits.  Specifically, the operation will be applied as...
     *
     * WR(cmd, (RD(cmd) & ~clr) | set)
     *
     * @param clr_bits The mask of bits to be cleared.
     * @param clr_bits The mask of bits to be set.
     * @return A status_t indicating success or failure of the operation.
     */
    status_t ModifyCmd(uint16_t clr_bits, uint16_t set_bits);

    /*
     * Enable or disable bus mastering in a device's configuration.
     *
     * @param enable If true, allow the device to access main system memory as a bus
     * master.
     * @return A status_t indicating success or failure of the operation.
     */
    inline status_t EnableBusMaster(bool enabled) {
        if (enabled && disabled_)
            return ERR_BAD_STATE;

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_BUS_MASTER_EN,
                         enabled ? PCI_COMMAND_BUS_MASTER_EN : 0);
    }

    /*
     * Enable or disable PIO access in a device's configuration.
     *
     * @param enable If true, allow the device to access its PIO mapped registers.
     * @return A status_t indicating success or failure of the operation.
     */
    inline status_t EnablePio(bool enabled) {
        if (enabled && disabled_)
            return ERR_BAD_STATE;

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_IO_EN,
                         enabled ? PCI_COMMAND_IO_EN : 0);
    }

    /*
     * Enable or disable MMIO access in a device's configuration.
     *
     * @param enable If true, allow the device to access its MMIO mapped registers.
     * @return A status_t indicating success or failure of the operation.
     */
    inline status_t EnableMmio(bool enabled) {
        if (enabled && disabled_)
            return ERR_BAD_STATE;

        return ModifyCmd(enabled ? 0 : PCI_COMMAND_MEM_EN,
                         enabled ? PCI_COMMAND_MEM_EN : 0);
    }


    /*
     * Return information about the requested base address register, if it has been
     * allocated.  Otherwise, return NULL.
     *
     * @param bar_ndx The index of the BAR register to fetch info for.
     *
     * @return A pointer to the BAR info, including where in the bus address space
     * the BAR window has been mapped, or NULL if the BAR window does not exist or
     * has not been allocated.
     */
    const pcie_bar_info_t* GetBarInfo(uint bar_ndx) const {
        if (bar_ndx >= bar_count_)
            return nullptr;

        DEBUG_ASSERT(bar_ndx < countof(bars_));

        const pcie_bar_info_t* ret = &bars_[bar_ndx];
        return (!disabled_ && (ret->allocation != nullptr)) ? ret : nullptr;
    }

    /**
     * Query the number of IRQs which are supported for a given IRQ mode by a given
     * device.
     *
     * @param mode The IRQ mode to query capabilities for.
     * @param out_caps A pointer to structure which, upon success, will hold the
     * capabilities of the selected IRQ mode.
     *
     * @return A status_t indicating the success or failure of the operation.
     */
    status_t QueryIrqModeCapabilities(pcie_irq_mode_t mode,
                                      pcie_irq_mode_caps_t* out_caps) const;

    /**
     * Fetch details about the currently configured IRQ mode.
     *
     * @param out_info A pointer to the structure which (upon success) will hold
     * info about the currently configured IRQ mode.  @see pcie_irq_mode_info_t for
     * more details.
     *
     * @return A status_t indicating the success or failure of the operation.
     * Status codes may include (but are not limited to)...
     *
     * ++ ERR_UNAVAILABLE
     *    The device has become unplugged and is waiting to be released.
     */
    status_t GetIrqMode(pcie_irq_mode_info_t* out_info) const;

    /**
     * Configure the base IRQ mode, requesting a specific number of vectors and
     * sharing mode in the process.
     *
     * Devices are not permitted to transition from an active mode (anything but
     * DISABLED) to a different active mode.  They must first transition to
     * DISABLED, then request the new mode.
     *
     * Transitions to the DISABLED state will automatically mask and un-register all
     * IRQ handlers, and return all allocated resources to the system pool.  IRQ
     * dispatch may continue to occur for unmasked IRQs during a transition to
     * DISABLED, but is guaranteed not to occur after the call to pcie_set_irq_mode
     * has completed.
     *
     * @param mode The requested mode.
     * @param requested_irqs The number of individual IRQ vectors the device would
     * like to use.
     *
     * @return A status_t indicating the success or failure of the operation.
     * Status codes may include (but are not limited to)...
     *
     * ++ ERR_UNAVAILABLE
     *    The device has become unplugged and is waiting to be released.
     * ++ ERR_BAD_STATE
     *    The device cannot transition into the selected mode at this point in time
     *    due to the mode it is currently in.
     * ++ ERR_NOT_SUPPORTED
     *    ++ The chosen mode is not supported by the device
     *    ++ The device supports the chosen mode, but does not support the number of
     *       IRQs requested.
     * ++ ERR_NO_RESOURCES
     *    The system is unable to allocate sufficient system IRQs to satisfy the
     *    number of IRQs and exclusivity mode requested the device driver.
     */
    status_t SetIrqMode(pcie_irq_mode_t mode, uint requested_irqs);

    /**
     * Set the current IRQ mode to PCIE_IRQ_MODE_DISABLED
     *
     * Convenience function.  @see SetIrqMode for details.
     */
    void SetIrqModeDisabled() {
        /* It should be impossible to fail a transition to the DISABLED state,
         * regardless of the state of the system.  ASSERT this in debug builds */
        __UNUSED status_t result;

        result = SetIrqMode(PCIE_IRQ_MODE_DISABLED, 0);

        DEBUG_ASSERT(result == NO_ERROR);
    }

    /**
     * Register an IRQ handler for the specified IRQ ID.
     *
     * @param irq_id The ID of the IRQ to register.
     * @param handler A pointer to the handler function to call when the IRQ is
     * received.  Pass NULL to automatically mask the IRQ and unregister the
     * handler.
     * @param ctx A user supplied context pointer to pass to a registered handler.
     *
     * @return A status_t indicating the success or failure of the operation.
     * Status codes may include (but are not limited to)...
     *
     * ++ ERR_UNAVAILABLE
     *    The device has become unplugged and is waiting to be released.
     * ++ ERR_BAD_STATE
     *    The device is in DISABLED IRQ mode.
     * ++ ERR_INVALID_ARGS
     *    The irq_id parameter is out of range for the currently configured mode.
     */
    status_t RegisterIrqHandler(uint irq_id, pcie_irq_handler_fn_t handler, void* ctx);

    /**
     * Mask or unmask the specified IRQ for the given device.
     *
     * @param irq_id The ID of the IRQ to mask or unmask.
     * @param mask If true, mask (disable) the IRQ.  Otherwise, unmask it.
     *
     * @return A status_t indicating the success or failure of the operation.
     * Status codes may include (but are not limited to)...
     *
     * ++ ERR_UNAVAILABLE
     *    The device has become unplugged and is waiting to be released.
     * ++ ERR_BAD_STATE
     *    Attempting to mask or unmask an IRQ while in the DISABLED mode or with no
     *    handler registered.
     * ++ ERR_INVALID_ARGS
     *    The irq_id parameter is out of range for the currently configured mode.
     * ++ ERR_NOT_SUPPORTED
     *    The device is operating in MSI mode, but neither the PCI device nor the
     *    platform interrupt controller support masking the MSI vector.
     */
    status_t MaskUnmaskIrq(uint irq_id, bool mask);

    void SetQuirksDone() { quirks_done_ = true; }

    /**
     * Convenience functions.  @see MaskUnmaskIrq for details.
     */
    status_t MaskIrq(uint irq_id)   { return MaskUnmaskIrq(irq_id, true); }
    status_t UnmaskIrq(uint irq_id) { return MaskUnmaskIrq(irq_id, false); }

    const PciConfig*     config()      const { return cfg_; }
    paddr_t              config_phys() const { return cfg_phys_; }
    mxtl::RefPtr<VmObject> config_vmo() const { return cfg_vmo_; }
    PcieBusDriver&       driver()            { return bus_drv_; }

    bool     plugged_in()     const { return plugged_in_; }
    bool     disabled()       const { return disabled_; }
    bool     claimed()        const { return claimed_; }
    bool     quirks_done()    const { return quirks_done_; }

    bool     is_bridge()      const { return is_bridge_; }
    bool     is_pcie()        const { return (pcie_ != nullptr); }
    uint16_t vendor_id()      const { return vendor_id_; }
    uint16_t device_id()      const { return device_id_; }
    uint8_t  class_id()       const { return class_id_; }
    uint8_t  subclass()       const { return subclass_; }
    uint8_t  prog_if()        const { return prog_if_; }
    uint8_t  rev_id()         const { return rev_id_; }
    uint     bus_id()         const { return bus_id_; }
    uint     dev_id()         const { return dev_id_; }
    uint     func_id()        const { return func_id_; }
    uint     bar_count()      const { return bar_count_; }
    uint8_t  legacy_irq_pin() const { return irq_.legacy.pin; }
    const    CapabilityList& capabilities() const { return caps_.detected; }
    // TODO(cja): This doesn't really make sense in a pcie capability optional world.
    // It is only used by bridge and debug code, so it might make sense to just have those check if
    // the device is pcie first, then use dev->pcie()->devtype().
    pcie_device_type_t pcie_device_type() const {
        if (pcie_)
            return pcie_->devtype();
        else
            return PCIE_DEVTYPE_UNKNOWN;
    }

    // TODO(johngro) : make these protected.  They are currently only visibile
    // because of debug code.
    Mutex* dev_lock() { return &dev_lock_; }

protected:
    friend class PcieUpstreamNode;
    friend class PcieBusDriver;  // TODO(johngro): remove this.  Currently used for IRQ swizzle.
    PcieDevice(PcieBusDriver& bus_drv, uint bus_id, uint dev_id, uint func_id, bool is_bridge);

    void ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits);
    void AssignCmdLocked(uint16_t value) { ModifyCmdLocked(0xFFFF, value); }

    // Initialization and probing.
    status_t Init(PcieUpstreamNode& upstream);
    status_t InitLocked(PcieUpstreamNode& upstream);
    status_t ProbeBarsLocked();
    status_t ProbeBarLocked(uint bar_id);
    status_t ProbeCapabilitiesLocked();
    status_t ParseStdCapabilitiesLocked();
    status_t ParseExtCapabilitiesLocked();
    status_t MapPinToIrqLocked(mxtl::RefPtr<PcieUpstreamNode>&& upstream);
    status_t InitLegacyIrqStateLocked(PcieUpstreamNode& upstream);

    // BAR allocation
    virtual status_t AllocateBars();
    status_t         AllocateBarsLocked();
    status_t         AllocateBarLocked(pcie_bar_info_t& info);

    // Disable a device, and anything downstream of it.  The device will
    // continue to enumerate, but users will only be able to access config (and
    // only in a read only fashion).  BAR windows, bus mastering, and interrupts
    // will all be disabled.
    virtual void Disable();
    void         DisableLocked();

    PcieBusDriver& bus_drv_;        // Reference to our bus driver state.
    const PciConfig*         cfg_ = nullptr;  // Pointer to the memory mapped ECAM (kernel vaddr)
    paddr_t        cfg_phys_ = 0;   // The physical address of the device's ECAM
    mxtl::RefPtr<VmObject> cfg_vmo_ = nullptr;
    SpinLock       cmd_reg_lock_;   // Protection for access to the command register.
    const bool     is_bridge_;      // True if this device is also a bridge
    const uint     bus_id_;         // The bus ID this bridge/device exists on
    const uint     dev_id_;         // The device ID of this bridge/device
    const uint     func_id_;        // The function ID of this bridge/device
    uint16_t       vendor_id_;      // The device's vendor ID, as read from config
    uint16_t       device_id_;      // The device's device ID, as read from config
    uint8_t        class_id_;       // The device's class ID, as read from config.
    uint8_t        subclass_;       // The device's subclass, as read from config.
    uint8_t        prog_if_;        // The device's programming interface (from cfg)
    uint8_t        rev_id_;         // The device's revision ID (from cfg)

    mxtl::RefPtr<PcieUpstreamNode> upstream_;  // The upstream node in the device graph.

    /* State related to lifetime management */
    mutable Mutex dev_lock_;
    bool          plugged_in_  = false;
    bool          disabled_    = false;
    bool          claimed_     = false;
    bool          quirks_done_ = false;

    /* Info about the BARs computed and cached during the initial setup/probe,
     * indexed by starting BAR register index */
    pcie_bar_info_t bars_[PCIE_MAX_BAR_REGS];
    const uint bar_count_;

private:
    friend class SharedLegacyIrqHandler;

    // Top level internal IRQ support.
    status_t QueryIrqModeCapabilitiesLocked(pcie_irq_mode_t mode,
                                            pcie_irq_mode_caps_t* out_caps) const;
    status_t GetIrqModeLocked(pcie_irq_mode_info_t* out_info) const;
    status_t SetIrqModeLocked(pcie_irq_mode_t mode, uint requested_irqs);
    status_t RegisterIrqHandlerLocked(uint irq_id, pcie_irq_handler_fn_t handler, void* ctx);
    status_t MaskUnmaskIrqLocked(uint irq_id, bool mask);

    // Internal Legacy IRQ support.
    status_t MaskUnmaskLegacyIrq(bool mask);
    status_t EnterLegacyIrqMode(uint requested_irqs);
    void     LeaveLegacyIrqMode();

    // Internal MSI IRQ support.
    void SetMsiEnb(bool enb) {
        DEBUG_ASSERT(irq_.msi);
        DEBUG_ASSERT(irq_.msi->is_valid());
        cfg_->Write(irq_.msi->ctrl_reg(),
                PCIE_CAP_MSI_CTRL_SET_ENB(enb, cfg_->Read(irq_.msi->ctrl_reg())));
    }

    bool     MaskUnmaskMsiIrqLocked(uint irq_id, bool mask);
    status_t MaskUnmaskMsiIrq(uint irq_id, bool mask);
    void     MaskAllMsiVectors();
    void     SetMsiTarget(uint64_t tgt_addr, uint32_t tgt_data);
    void     FreeMsiBlock();
    void     SetMsiMultiMessageEnb(uint requested_irqs);
    void     LeaveMsiIrqMode();
    status_t EnterMsiIrqMode(uint requested_irqs);

    enum handler_return        MsiIrqHandler(pcie_irq_handler_state_t& hstate);
    static enum handler_return MsiIrqHandlerThunk(void *arg);

    // Common Internal IRQ support.
    void     ResetCommonIrqBookkeeping();
    status_t AllocIrqHandlers(uint requested_irqs, bool is_masked);

    /* Capabilities */
    // TODO(cja): organize capabilities into their own structure
    struct Capabilities {
        CapabilityList detected;
    } caps_;
    /* PCI Express Capabilities (Standard Capability 0x10) if present */
    PciCapPcie* pcie_ = nullptr;
    /* PCI Advanced Capabilities (Standard Capability 0x13) if present */
    PciCapAdvFeatures* pci_af_ = nullptr;

    // IRQ configuration and handling state
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
            uint    irq_id = static_cast<uint>(-1);
            struct list_node shared_handler_node = { nullptr, nullptr};
            mxtl::RefPtr<SharedLegacyIrqHandler> shared_handler;
        } legacy;

        PciCapMsi* msi = nullptr;
        /* TODO(johngro) : Add MSI-X state */
        struct { } msi_x;
    } irq_;
};
