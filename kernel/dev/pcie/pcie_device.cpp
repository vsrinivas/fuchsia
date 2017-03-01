// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <dev/pcie_bridge.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_device.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_physical.h>
#include <list.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include <mxalloc/new.h>

#define LOCAL_TRACE 0

namespace {  // anon namespace.  Externals do not need to know about PcieDeviceImpl
class PcieDeviceImpl : public PcieDevice {
public:
    static mxtl::RefPtr<PcieDevice> Create(PcieUpstreamNode& upstream, uint dev_id, uint func_id);

    // Disallow copying, assigning and moving.
    DISALLOW_COPY_ASSIGN_AND_MOVE(PcieDeviceImpl);

    // Implement ref counting, do not let derived classes override.
    PCIE_IMPLEMENT_REFCOUNTED;

protected:
    PcieDeviceImpl(PcieBusDriver& bus_drv, uint bus_id, uint dev_id, uint func_id)
        : PcieDevice(bus_drv, bus_id, dev_id, func_id, false) { }
};

mxtl::RefPtr<PcieDevice> PcieDeviceImpl::Create(PcieUpstreamNode& upstream,
                                                uint dev_id, uint func_id) {
    AllocChecker ac;
    auto raw_dev = new (&ac) PcieDeviceImpl(upstream.driver(),
                                            upstream.managed_bus_id(),
                                            dev_id,
                                            func_id);
    if (!ac.check()) {
        TRACEF("Out of memory attemping to create PCIe device %02x:%02x.%01x.\n",
                upstream.managed_bus_id(), dev_id, func_id);
        return nullptr;
    }

    auto dev = mxtl::AdoptRef(static_cast<PcieDevice*>(raw_dev));
    status_t res = raw_dev->Init(upstream);
    if (res != NO_ERROR) {
        TRACEF("Failed to initialize PCIe device %02x:%02x.%01x. (res %d)\n",
                upstream.managed_bus_id(), dev_id, func_id, res);
        return nullptr;
    }

    return dev;
}
}  // namespace

PcieDevice::PcieDevice(PcieBusDriver& bus_drv,
                       uint bus_id, uint dev_id, uint func_id, bool is_bridge)
    : bus_drv_(bus_drv),
      is_bridge_(is_bridge),
      bus_id_(bus_id),
      dev_id_(dev_id),
      func_id_(func_id),
      bar_count_(is_bridge ? PCIE_BAR_REGS_PER_BRIDGE : PCIE_BAR_REGS_PER_DEVICE) {
}

PcieDevice::~PcieDevice() {
    /* We should already be unlinked from the bus's device tree. */
    DEBUG_ASSERT(!upstream_);
    DEBUG_ASSERT(!plugged_in_);

    /* By the time we destruct, we had better not be claimed anymore */
    DEBUG_ASSERT(!claimed_);

    /* TODO(johngro) : ASSERT that this device no longer participating in any of
     * the bus driver's shared IRQ dispatching. */

    /* Make certain that all bus access (MMIO, PIO, Bus mastering) has been
     * disabled.  Also, explicitly disable legacy IRQs */
    if (cfg_)
        cfg_->Write(PciConfig::kCommand, PCIE_CFG_COMMAND_INT_DISABLE);
}

mxtl::RefPtr<PcieDevice> PcieDevice::Create(PcieUpstreamNode& upstream, uint dev_id, uint func_id) {
    return PcieDeviceImpl::Create(upstream, dev_id, func_id);
}

status_t PcieDevice::Init(PcieUpstreamNode& upstream) {
    AutoLock dev_lock(&dev_lock_);

    status_t res = InitLocked(upstream);
    if (res == NO_ERROR) {
        // Things went well, flag the device as plugged in and link ourselves up to
        // the graph.
        plugged_in_ = true;
        bus_drv_.LinkDeviceToUpstream(*this, upstream);
    }

    return res;
}

status_t PcieDevice::InitLocked(PcieUpstreamNode& upstream) {
    status_t res;
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(cfg_ == nullptr);

    cfg_ = bus_drv_.GetConfig(bus_id_, dev_id_, func_id_, &cfg_phys_);
    if (cfg_ == nullptr) {
        TRACEF("Failed to fetch config for device %02x:%02x.%01x.\n", bus_id_, dev_id_, func_id_);
        return ERR_BAD_STATE;
    }

    // Cache basic device info
    vendor_id_ = cfg_->Read(PciConfig::kVendorId);
    device_id_ = cfg_->Read(PciConfig::kDeviceId);
    class_id_  = cfg_->Read(PciConfig::kBaseClass);
    subclass_  = cfg_->Read(PciConfig::kSubClass);
    prog_if_   = cfg_->Read(PciConfig::kProgramInterface);
    rev_id_    = cfg_->Read(PciConfig::kRevisionId);

    // Determine the details of each of the BARs, but do not actually allocate
    // space on the bus for them yet.
    res = ProbeBarsLocked();
    if (res != NO_ERROR)
        return res;

    // Parse and sanity check the capabilities and extended capabilities lists
    // if they exist
    res = ProbeCapabilitiesLocked();
    if (res != NO_ERROR)
        return res;

    // Now that we know what our capabilities are, initialize our internal IRQ
    // bookkeeping
    res = InitLegacyIrqStateLocked(upstream);
    if (res != NO_ERROR)
        return res;

    // Map a VMO to the config if it's mappable via MMIO.
    if (cfg_->addr_space() == PciAddrSpace::MMIO) {
        cfg_vmo_ = VmObjectPhysical::Create(cfg_phys_, PAGE_SIZE);
        if (cfg_vmo_ == nullptr) {
            TRACEF("Failed to allocate VMO for config of device %02x:%02x:%01x!\n", bus_id_, dev_id_, func_id_);
            return ERR_NO_MEMORY;
        }

        // Unlike BARs, this should always be treated like device memory
        cfg_vmo_->SetMappingCachePolicy(ARCH_MMU_FLAG_UNCACHED_DEVICE);
    }

    return NO_ERROR;
}

mxtl::RefPtr<PcieUpstreamNode> PcieDevice::GetUpstream() {
    return bus_drv_.GetUpstream(*this);
}

status_t PcieDevice::Claim() {
    AutoLock dev_lock(&dev_lock_);

    /* Has the device already been claimed? */
    if (claimed_)
        return ERR_ALREADY_BOUND;

    /* Has the device been unplugged or disabled? */
    if (!plugged_in_ || disabled_)
        return ERR_UNAVAILABLE;

    /* Looks good!  Claim the device. */
    claimed_ = true;

    return NO_ERROR;
}

void PcieDevice::Unclaim() {
    AutoLock dev_lock(&dev_lock_);

    // Nothing to do if we are not claimed.
    if (!claimed_)
        return;

    LTRACEF("Unclaiming PCI device %02x:%02x.%x...\n", bus_id_, dev_id_, func_id_);

    /* Make sure that all IRQs are shutdown and all handlers released for this device */
    SetIrqModeLocked(PCIE_IRQ_MODE_DISABLED, 0);

    /* If this device is not a bridge, disable access to MMIO windows, PIO windows, and system
     * memory.  If it is a bridge, leave this stuff turned on so that downstream devices can
     * continue to function. */
    if (!is_bridge_)
        cfg_->Write(PciConfig::kCommand, PCIE_CFG_COMMAND_INT_DISABLE);

    /* Device is now unclaimed */
    claimed_ = false;
}

void PcieDevice::Unplug() {
    /* Begin by completely nerfing this device, and preventing an new API
     * operations on it.  We need to be inside the dev lock to do this.  Note:
     * it is assumed that we will not disappear during any of this function,
     * because our caller is holding a reference to us. */
    AutoLock dev_lock(&dev_lock_);

    /* For now ASSERT that we are not claimed.  Moving forward, we need to
     * inform our owner that we have been suddenly hot-unplugged.
     */
    DEBUG_ASSERT(!claimed_);

    if (plugged_in_) {
        /* Remove all access this device has to the PCI bus */
        cfg_->Write(PciConfig::kCommand, PCIE_CFG_COMMAND_INT_DISABLE);

        /* TODO(johngro) : Make sure that our interrupt mode has been set to
         * completely disabled.  Do not return allocated BARs to the central
         * pool yet.  These regions of the physical bus need to remain
         * "allocated" until all drivers/users in the system release their last
         * reference to the device.  This way, if the device gets plugged in
         * again immediately, the new version of the device will not end up
         * getting mapped underneath any stale driver instances. */

        plugged_in_ = false;
    } else {
        /* TODO(johngro) : Assert that the device has been completely disabled. */
    }

    /* Unlink ourselves from our upstream parent (if we still have one). */
    bus_drv_.UnlinkDeviceFromUpstream(*this);
}

status_t PcieDevice::DoFunctionLevelReset() {
    status_t ret;

    // TODO(johngro) : Function level reset is an operation which can take quite
    // a long time (more than a second).  We should not hold the device lock for
    // the entire duration of the operation.  This should be re-done so that the
    // device can be placed into a "resetting" state (and other API calls can
    // fail with ERR_BAD_STATE, or some-such) and the lock can be released while the
    // reset timeouts run.  This way, a spontaneous unplug event can occur and
    // not block the whole world because the device unplugged was in the process
    // of a FLR.
    AutoLock dev_lock(&dev_lock_);

    // Make certain to check to see if the device is still plugged in.
    if (!plugged_in_)
        return ERR_UNAVAILABLE;

    // Disallow reset if we currently have an active IRQ mode.
    //
    // Note: the only possible reason for get_irq_mode to fail would be for the
    // device to be unplugged.  Since we have already checked for that, we
    // assert that the call should succeed.
    pcie_irq_mode_info_t irq_mode_info;
    ret = GetIrqModeLocked(&irq_mode_info);
    DEBUG_ASSERT(NO_ERROR == ret);

    if (irq_mode_info.mode != PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(!irq_mode_info.registered_handlers);
    DEBUG_ASSERT(!irq_mode_info.max_handlers);

    // If cannot reset via the PCIe capability, or the PCI advanced capability,
    // then this device simply does not support function level reset.
    if (!(pcie_ && pcie_->has_flr()) && !(pci_af_ && pci_af_->has_flr()))
        return ERR_NOT_SUPPORTED;

    // Pick the functions we need for testing whether or not transactions are
    // pending for this device, and for initiating the FLR
    bool (*check_trans_pending)(void* ctx);
    void (*initiate_flr)(void* ctx);

    if (pcie_ && pcie_->has_flr()) {
        check_trans_pending = [](void* ctx) -> bool {
            auto thiz = reinterpret_cast<PcieDevice*>(ctx);
            return thiz->cfg_->Read(thiz->pcie_->device.status()) &
                                    PCS_DEV_STS_TRANSACTIONS_PENDING;
        };
        initiate_flr = [](void* ctx) {
            auto thiz = reinterpret_cast<PcieDevice*>(ctx);
            auto val = static_cast<uint16_t>(thiz->cfg_->Read(thiz->pcie_->device.ctrl()) |
                                                              PCS_DEV_CTRL_INITIATE_FLR);
            thiz->cfg_->Write(thiz->pcie_->device.ctrl(), val);
        };
    } else {
        check_trans_pending = [](void* ctx) -> bool {
            auto thiz = reinterpret_cast<PcieDevice*>(ctx);
            return thiz->cfg_->Read(thiz->pci_af_->af_status()) & PCS_ADVCAPS_STATUS_TRANS_PENDING;
        };
        initiate_flr = [](void* ctx) {
            auto thiz = reinterpret_cast<PcieDevice*>(ctx);
            thiz->cfg_->Write(thiz->pci_af_->af_ctrl(), PCS_ADVCAPS_CTRL_INITIATE_FLR);
        };
    }

    // Following the procedure outlined in the Implementation notes
    uint32_t bar_backup[PCIE_MAX_BAR_REGS];
    uint16_t cmd_backup;

    // 1) Make sure driver code is not creating new transactions (not much I
    //    can do about this, just have to hope).
    // 2) Clear out the command register so that no new transactions may be
    //    initiated.  Also back up the BARs in the process.
    {
        DEBUG_ASSERT(irq_.legacy.shared_handler != nullptr);
        AutoSpinLockIrqSave cmd_reg_lock(cmd_reg_lock_);

        cmd_backup = cfg_->Read(PciConfig::kCommand);
        cfg_->Write(PciConfig::kCommand, PCIE_CFG_COMMAND_INT_DISABLE);
        for (uint i = 0; i < bar_count_; ++i)
            bar_backup[i] = cfg_->Read(PciConfig::kBAR(i));
    }

    // 3) Poll the transaction pending bit until it clears.  This may take
    //    "several seconds"
    lk_time_t start = current_time();
    ret = ERR_TIMED_OUT;
    do {
        if (!check_trans_pending(this)) {
            ret = NO_ERROR;
            break;
        }
        thread_sleep_relative(LK_MSEC(1));
    } while ((current_time() - start) < LK_SEC(5));

    if (ret != NO_ERROR) {
        TRACEF("Timeout waiting for pending transactions to clear the bus "
               "for %02x:%02x.%01x\n",
               bus_id_, dev_id_, func_id_);

        // Restore the command register
        AutoSpinLockIrqSave cmd_reg_lock(cmd_reg_lock_);
        cfg_->Write(PciConfig::kCommand, cmd_backup);

        return ret;
    } else {
        // 4) Software initiates the FLR
        initiate_flr(this);

        // 5) Software waits 100mSec
        thread_sleep_relative(LK_MSEC(100));
    }

    // NOTE: Even though the spec says that the reset operation is supposed
    // to always take less than 100mSec, no one really follows this rule.
    // Generally speaking, when a device resets, config read cycles will
    // return all 0xFFs until the device finally resets and comes back.
    // Poll the Vendor ID field until the device finally completes it's
    // reset.
    start = current_time();
    ret   = ERR_TIMED_OUT;
    do {
        if (cfg_->Read(PciConfig::kVendorId) != PCIE_INVALID_VENDOR_ID) {
            ret = NO_ERROR;
            break;
        }
        thread_sleep_relative(LK_MSEC(1));
    } while ((current_time() - start) < LK_SEC(5));

    if (ret == NO_ERROR) {
        // 6) Software reconfigures the function and enables it for normal operation
        AutoSpinLockIrqSave cmd_reg_lock(cmd_reg_lock_);

        for (uint i = 0; i < bar_count_; ++i)
            cfg_->Write(PciConfig::kBAR(i), bar_backup[i]);
        cfg_->Write(PciConfig::kCommand, cmd_backup);
    } else {
        // TODO(johngro) : What do we do if this fails?  If we trigger a
        // device reset, and the device fails to re-appear after 5 seconds,
        // it is probably gone for good.  We probably need to force unload
        // any device drivers which had previously owned the device.
        TRACEF("Timeout waiting for %02x:%02x.%01x to complete function "
               "level reset.  This is Very Bad.\n",
               bus_id_, dev_id_, func_id_);
    }

    return ret;
}

status_t PcieDevice::ModifyCmd(uint16_t clr_bits, uint16_t set_bits) {
    AutoLock dev_lock(&dev_lock_);

    /* In order to keep internal bookkeeping coherent, and interactions between
     * MSI/MSI-X and Legacy IRQ mode safe, API users may not directly manipulate
     * the legacy IRQ enable/disable bit.  Just ignore them if they try to
     * manipulate the bit via the modify cmd API. */
    clr_bits = static_cast<uint16_t>(clr_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);
    set_bits = static_cast<uint16_t>(set_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);

    if (plugged_in_) {
        ModifyCmdLocked(clr_bits, set_bits);
        return NO_ERROR;
    }

    return ERR_UNAVAILABLE;
}

void PcieDevice::ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) {
    DEBUG_ASSERT(dev_lock_.IsHeld());

    {
        AutoSpinLockIrqSave cmd_reg_lock(cmd_reg_lock_);
        cfg_->Write(PciConfig::kCommand,
                     static_cast<uint16_t>((cfg_->Read(PciConfig::kCommand) & ~clr_bits)
                                                                             |  set_bits));
    }
}

status_t PcieDevice::ProbeBarsLocked() {
    DEBUG_ASSERT(cfg_);
    DEBUG_ASSERT(dev_lock_.IsHeld());

    static_assert(PCIE_MAX_BAR_REGS >= PCIE_BAR_REGS_PER_DEVICE, "");
    static_assert(PCIE_MAX_BAR_REGS >= PCIE_BAR_REGS_PER_BRIDGE, "");

    __UNUSED uint8_t header_type = cfg_->Read(PciConfig::kHeaderType) & PCI_HEADER_TYPE_MASK;

    DEBUG_ASSERT((header_type == PCI_HEADER_TYPE_STANDARD) ||
                 (header_type == PCI_HEADER_TYPE_PCI_BRIDGE));
    DEBUG_ASSERT(bar_count_ <= countof(bars_));

    for (uint i = 0; i < bar_count_; ++i) {
        /* If this is a re-scan of the bus, We should not be re-enumerating BARs. */
        DEBUG_ASSERT(bars_[i].size == 0);
        DEBUG_ASSERT(bars_[i].allocation == nullptr);

        status_t probe_res = ProbeBarLocked(i);
        if (probe_res != NO_ERROR)
            return probe_res;

        if (bars_[i].size > 0) {
            /* If this was a 64 bit bar, it took two registers to store.  Make
             * sure to skip the next register */
            if (bars_[i].is_64bit) {
                i++;

                if (i >= bar_count_) {
                    TRACEF("Device %02x:%02x:%01x claims to have 64-bit BAR in position %u/%u!\n",
                           bus_id_, dev_id_, func_id_, i, bar_count_);
                    return ERR_BAD_STATE;
                }
            }
        }
    }

    return NO_ERROR;
}

status_t PcieDevice::ProbeBarLocked(uint bar_id) {
    DEBUG_ASSERT(cfg_);
    DEBUG_ASSERT(bar_id < bar_count_);
    DEBUG_ASSERT(bar_id < countof(bars_));

    /* Determine the type of BAR this is.  Make sure that it is one of the types we understand */
    pcie_bar_info_t& bar_info  = bars_[bar_id];
    uint32_t bar_val           = cfg_->Read(PciConfig::kBAR(bar_id));
    bar_info.is_mmio           = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
    bar_info.is_64bit          = bar_info.is_mmio &&
                                 ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
    bar_info.is_prefetchable   = bar_info.is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);
    bar_info.first_bar_reg     = bar_id;

    if (bar_info.is_64bit) {
        if ((bar_id + 1) >= bar_count_) {
            TRACEF("Illegal 64-bit MMIO BAR position (%u/%u) while fetching BAR info "
                   "for device config @%p\n",
                   bar_id, bar_count_, cfg_);
            return ERR_BAD_STATE;
        }
    } else {
        if (bar_info.is_mmio && ((bar_val & PCI_BAR_MMIO_TYPE_MASK) != PCI_BAR_MMIO_TYPE_32BIT)) {
            TRACEF("Unrecognized MMIO BAR type (BAR[%u] == 0x%08x) while fetching BAR info "
                   "for device config @%p\n",
                   bar_id, bar_val, cfg_);
            return ERR_BAD_STATE;
        }
    }

    /* Disable either MMIO or PIO (depending on the BAR type) access while we
     * perform the probe.  We don't want the addresses written during probing to
     * conflict with anything else on the bus.  Note:  No drivers should have
     * acccess to this device's registers during the probe process as the device
     * should not have been published yet.  That said, there could be other
     * (special case) parts of the system accessing a devices registers at this
     * point in time, like an early init debug console or serial port.  Don't
     * make any attempt to print or log until the probe operation has been
     * completed.  Hopefully these special systems are quiescent at this point
     * in time, otherwise they might see some minor glitching while access is
     * disabled.
     */
    uint16_t backup = cfg_->Read(PciConfig::kCommand);;
    if (bar_info.is_mmio)
        cfg_->Write(PciConfig::kCommand, static_cast<uint16_t>(backup & ~PCI_COMMAND_MEM_EN));
    else
        cfg_->Write(PciConfig::kCommand, static_cast<uint16_t>(backup & ~PCI_COMMAND_IO_EN));

    /* Figure out the size of this BAR region by writing 1's to the
     * address bits, then reading back to see which bits the device
     * considers un-configurable. */
    uint32_t addr_mask = bar_info.is_mmio ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;
    uint32_t addr_lo   = bar_val & addr_mask;
    uint64_t size_mask;

    cfg_->Write(PciConfig::kBAR(bar_id), bar_val | addr_mask);
    size_mask = ~(cfg_->Read(PciConfig::kBAR(bar_id)) & addr_mask);
    cfg_->Write(PciConfig::kBAR(bar_id), bar_val);

    if (bar_info.is_mmio) {
        if (bar_info.is_64bit) {

            /* 64bit MMIO? Probe the upper bits as well */
            bar_id++;
            bar_val = cfg_->Read(PciConfig::kBAR(bar_id));
            cfg_->Write(PciConfig::kBAR(bar_id), 0xFFFFFFFF);
            size_mask |= ((uint64_t)~cfg_->Read(PciConfig::kBAR(bar_id))) << 32;
            cfg_->Write(PciConfig::kBAR(bar_id), bar_val);
            bar_info.size = size_mask + 1;
            bar_info.bus_addr = (static_cast<uint64_t>(bar_val) << 32) | addr_lo;
        } else {
            bar_info.size = (uint32_t)(size_mask + 1);
            bar_info.bus_addr = addr_lo;
        }
    } else {
        /* PIO BAR */
        bar_info.size = ((uint32_t)(size_mask + 1)) & PCIE_PIO_ADDR_SPACE_MASK;
        bar_info.bus_addr = addr_lo;
    }

    /* Restore the command register to its previous value */
    cfg_->Write(PciConfig::kCommand, backup);

    // Create a VMO mapping for this MMIO bar to hand out to clients. In
    // the event of PIO bars, the mx_pci_get_bar syscall will sort out
    // the PIO details from the info structure.
    if (bar_info.size > 0 && bar_info.is_mmio) {
        auto vmo = VmObjectPhysical::Create(bar_info.bus_addr,
                mxtl::max<uint64_t>(bar_info.size, PAGE_SIZE));
        if (vmo == nullptr) {
            TRACEF("Failed to allocate VMO for bar %u of device %02x:%02x:%01x!\n",
                    bar_id, bus_id_, dev_id_, func_id_);
            return ERR_NO_MEMORY;
        }

        // No cache policy is configured here so drivers may set it themselves
        bar_info.vmo = mxtl::move(vmo);
    }
    /* Success */
    return NO_ERROR;
}


status_t PcieDevice::AllocateBars() {
    AutoLock dev_lock(&dev_lock_);
    return AllocateBarsLocked();
}

status_t PcieDevice::AllocateBarsLocked() {
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(plugged_in_ && !claimed_);

    // Have we become unplugged?
    if (!plugged_in_)
        return ERR_UNAVAILABLE;

    // If this has been claimed by a driver, do not make any changes to the BAR
    // allocation.
    //
    // TODO(johngro) : kill this.  It should be impossible to become claimed if
    // we have not already allocated all of our BARs.
    if (claimed_)
        return NO_ERROR;

    /* Allocate BARs for the device */
    DEBUG_ASSERT(bar_count_ <= countof(bars_));
    for (size_t i = 0; i < bar_count_; ++i) {
        if (bars_[i].size) {
            status_t ret = AllocateBarLocked(bars_[i]);
            if (ret != NO_ERROR)
                return ret;
        }
    }

    return NO_ERROR;
}

status_t PcieDevice::AllocateBarLocked(pcie_bar_info_t& info) {
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(plugged_in_ && !claimed_);

    // Do not attempt to remap if we are rescanning the bus and this BAR is
    // already allocated, or if it does not exist (size is zero)
    if ((info.size == 0) || (info.allocation != nullptr))
        return NO_ERROR;

    // Hold a reference to our upstream node while we do this.  If we cannot
    // obtain a reference, then our upstream node has become unplugged and we
    // should just fail out now.
    auto upstream = GetUpstream();
    if (upstream == nullptr)
        return ERR_UNAVAILABLE;

    /* Does this BAR already have an assigned address?  If so, try to preserve
     * it, if possible. */
    if (info.bus_addr != 0) {
        RegionAllocator* alloc = nullptr;
        if (info.is_mmio) {
            /* We currently do not support preserving an MMIO region which spans
             * the 4GB mark.  If we encounter such a thing, clear out the
             * allocation and attempt to re-allocate. */
            uint64_t inclusive_end = info.bus_addr + info.size - 1;
            if (inclusive_end <= mxtl::numeric_limits<uint32_t>::max()) {
                alloc = &upstream->mmio_lo_regions();
            } else
            if (info.bus_addr > mxtl::numeric_limits<uint32_t>::max()) {
                alloc = &upstream->mmio_hi_regions();
            }
        } else {
            alloc = &upstream->pio_regions();
        }

        status_t res = ERR_NOT_FOUND;
        if (alloc != nullptr) {
            res = alloc->GetRegion({ .base = info.bus_addr, .size = info.size }, info.allocation);
        }

        if (res == NO_ERROR)
            return NO_ERROR;

        TRACEF("Failed to preserve device %02x:%02x.%01x's %s window "
               "[%#" PRIx64 ", %#" PRIx64 "] Attempting to re-allocate.\n",
               bus_id_, dev_id_, func_id_,
               info.is_mmio ? "MMIO" : "PIO",
               info.bus_addr, info.bus_addr + info.size - 1);
        info.bus_addr = 0;
    }

    /* We failed to preserve the allocation and need to attempt to
     * dynamically allocate a new region.  Close the device MMIO/PIO
     * windows, disable interrupts and shut of bus mastering (which will
     * also disable MSI interrupts) before we attempt dynamic allocation.
     */
    AssignCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE);

    /* Choose which region allocator we will attempt to allocate from, then
     * check to see if we have the space. */
    RegionAllocator* alloc = !info.is_mmio
                             ? &upstream->pio_regions()
                             : (info.is_64bit ? &upstream->mmio_hi_regions()
                                              : &upstream->mmio_lo_regions());
    uint32_t addr_mask = info.is_mmio
                       ? PCI_BAR_MMIO_ADDR_MASK
                       : PCI_BAR_PIO_ADDR_MASK;

    /* If check to see if we have the space to allocate within the chosen
     * range.  In the case of a 64 bit MMIO BAR, if we run out of space in
     * the high-memory MMIO range, try the low memory range as well.
     */
    while (true) {
        /* MMIO windows and I/O windows on systems where I/O space is actually
         * memory mapped must be aligned to a page boundary, at least. */
        bool     is_io_space = PCIE_HAS_IO_ADDR_SPACE && !info.is_mmio;
        uint64_t align_size  = ((info.size >= PAGE_SIZE) || is_io_space)
                             ? info.size
                             : PAGE_SIZE;
        status_t res = alloc->GetRegion(align_size, align_size, info.allocation);

        if (res != NO_ERROR) {
            if ((res == ERR_NOT_FOUND) && (alloc == &upstream->mmio_hi_regions())) {
                LTRACEF("Insufficient space to map 64-bit MMIO BAR in high region while "
                        "configuring BARs for device at %02x:%02x.%01x (cfg vaddr = %p).  "
                        "Falling back on low memory region.\n",
                        bus_id_, dev_id_, func_id_, cfg_);
                alloc = &upstream->mmio_lo_regions();
                continue;
            }

            TRACEF("Failed to dynamically allocate %s BAR region (size %#" PRIx64 ") "
                   "while configuring BARs for device at %02x:%02x.%01x (res = %d)\n",
                   info.is_mmio ? "MMIO" : "PIO", info.size,
                   bus_id_, dev_id_, func_id_, res);

            // Looks like we are out of luck.  Propagate the error up the stack
            // so that our upstream node knows to disable us.
            return res;
        }

        break;
    }

    /* Allocation succeeded.  Record our allocated and aligned physical address
     * in our BAR(s) */
    DEBUG_ASSERT(info.allocation != nullptr);
    uint bar_reg = info.first_bar_reg;
    info.bus_addr = info.allocation->base;

    cfg_->Write(PciConfig::kBAR(bar_reg), static_cast<uint32_t>((info.bus_addr & 0xFFFFFFFF) |
                                                (cfg_->Read(PciConfig::kBAR(bar_reg)) & ~addr_mask)));
    if (info.is_64bit)
        cfg_->Write(PciConfig::kBAR(bar_reg + 1), static_cast<uint32_t>(info.bus_addr >> 32));

    return NO_ERROR;
}

void PcieDevice::Disable() {
    DEBUG_ASSERT(!dev_lock_.IsHeld());
    AutoLock dev_lock(&dev_lock_);
    DisableLocked();
}

void PcieDevice::DisableLocked() {
    // Disable a device because we cannot allocate space for all of its BARs (or
    // forwarding windows, in the case of a bridge).  Flag the device as
    // disabled from here on out.
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(!claimed_);
    TRACEF("WARNING - Disabling device %02x:%02x.%01x due to unsatisfiable configuration\n",
            bus_id_, dev_id_, func_id_);

    // Flag the device as disabled.  Close the device's MMIO/PIO windows, shut
    // off device initiated accesses to the bus, disable legacy interrupts.
    // Basically, prevent the device from doing anything from here on out.
    disabled_ = true;
    AssignCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE);

    // Release all BAR allocations back into the pool they came from.
    for (auto& bar : bars_)
        bar.allocation = nullptr;
}
