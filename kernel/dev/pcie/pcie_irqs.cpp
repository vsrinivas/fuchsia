// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include <assert.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <dev/pcie_bus_driver.h>
#include <dev/pcie_bridge.h>
#include <dev/pcie_root.h>
#include <err.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

#include <dev/pci_config.h>
#include <dev/pcie_device.h>

#include <mxalloc/new.h>

#define LOCAL_TRACE 0

/******************************************************************************
 *
 * Helper routines common to all IRQ modes.
 *
 ******************************************************************************/
void PcieDevice::ResetCommonIrqBookkeeping() {
    if (irq_.handler_count > 1) {
        DEBUG_ASSERT(irq_.handlers != &irq_.singleton_handler);
        delete[] irq_.handlers;
    } else {
        DEBUG_ASSERT(irq_.handlers == &irq_.singleton_handler);
        irq_.singleton_handler.handler = nullptr;
        irq_.singleton_handler.ctx = nullptr;
        irq_.singleton_handler.dev = nullptr;
    }

    irq_.mode          = PCIE_IRQ_MODE_DISABLED;
    irq_.handlers      = nullptr;
    irq_.handler_count = 0;
}

status_t PcieDevice::AllocIrqHandlers(uint requested_irqs, bool is_masked) {
    DEBUG_ASSERT(requested_irqs);
    DEBUG_ASSERT(!irq_.handlers);
    DEBUG_ASSERT(!irq_.handler_count);

    if (requested_irqs == 1) {
        irq_.handlers      = &irq_.singleton_handler;
        irq_.handler_count = 1;
    } else {
        AllocChecker ac;
        irq_.handlers = new (&ac) pcie_irq_handler_state_t[requested_irqs];

        if (!ac.check())
            return ERR_NO_MEMORY;

        irq_.handler_count = requested_irqs;
    }

    for (uint i = 0; i < irq_.handler_count; ++i) {
        DEBUG_ASSERT(irq_.handlers[i].handler == nullptr);
        DEBUG_ASSERT(irq_.handlers[i].dev == nullptr);
        DEBUG_ASSERT(irq_.handlers[i].ctx == nullptr);
        irq_.handlers[i].dev        = this;
        irq_.handlers[i].pci_irq_id = i;
        irq_.handlers[i].masked     = is_masked;
    }

    return NO_ERROR;
}

/******************************************************************************
 *
 * Legacy IRQ mode routines.
 *
 ******************************************************************************/
mxtl::RefPtr<SharedLegacyIrqHandler> SharedLegacyIrqHandler::Create(uint irq_id) {
    AllocChecker ac;

    SharedLegacyIrqHandler* handler = new (&ac) SharedLegacyIrqHandler(irq_id);
    if (!ac.check()) {
        TRACEF("Failed to create shared legacry IRQ handler for system IRQ ID %u\n", irq_id);
        return nullptr;
    }

    return mxtl::AdoptRef(handler);
}

SharedLegacyIrqHandler::SharedLegacyIrqHandler(uint irq_id)
    : irq_id_(irq_id) {
    list_initialize(&device_handler_list_);
    mask_interrupt(irq_id_);  // This should not be needed, but just in case.
    register_int_handler(irq_id_, HandlerThunk, this);
}

SharedLegacyIrqHandler::~SharedLegacyIrqHandler() {
    DEBUG_ASSERT(list_is_empty(&device_handler_list_));
    mask_interrupt(irq_id_);
    register_int_handler(irq_id_, nullptr, nullptr);
}

enum handler_return SharedLegacyIrqHandler::Handler() {
    bool need_resched = false;

    /* Go over the list of device's which share this legacy IRQ and give them a
     * chance to handle any interrupts which may be pending in their device.
     * Keep track of whether or not any device has requested a re-schedule event
     * at the end of this IRQ. */
    AutoSpinLock list_lock(device_handler_list_lock_);

    if (list_is_empty(&device_handler_list_)) {
        TRACEF("Received legacy PCI INT (system IRQ %u), but there are no devices registered to "
               "handle this interrupt.  This is Very Bad.  Disabling the interrupt at the system "
               "IRQ level to prevent meltdown.\n",
               irq_id_);
        mask_interrupt(irq_id_);
        return INT_NO_RESCHEDULE;
    }

    PcieDevice* dev;
    list_for_every_entry(&device_handler_list_,
                         dev,
                         PcieDevice,
                         irq_.legacy.shared_handler_node) {
        uint16_t command, status;
        auto cfg = dev->config();

        {
            AutoSpinLock cmd_reg_lock(dev->cmd_reg_lock_);
            command = cfg->Read(PciConfig::kCommand);
            status  = cfg->Read(PciConfig::kStatus);
        }

        if ((status & PCIE_CFG_STATUS_INT_STS) && !(command & PCIE_CFG_COMMAND_INT_DISABLE)) {
            DEBUG_ASSERT(dev);
            pcie_irq_handler_state_t* hstate  = &dev->irq_.handlers[0];

            if (hstate) {
                pcie_irq_handler_retval_t irq_ret = PCIE_IRQRET_MASK;
                AutoSpinLock device_handler_lock(hstate->lock);

                if (hstate->handler) {
                    if (!hstate->masked)
                        irq_ret = hstate->handler(*dev, 0, hstate->ctx);

                    if (irq_ret & PCIE_IRQRET_RESCHED)
                        need_resched = true;
                } else {
                    TRACEF("Received legacy PCI INT (system IRQ %u) for %02x:%02x.%02x, but no irq_ "
                           "handler has been registered by the driver.  Force disabling the "
                           "interrupt.\n",
                           irq_id_, dev->bus_id_, dev->dev_id_, dev->func_id_);
                }

                if (irq_ret & PCIE_IRQRET_MASK) {
                    hstate->masked = true;
                    {
                        AutoSpinLock cmd_reg_lock(dev->cmd_reg_lock_);
                        command = cfg->Read(PciConfig::kCommand);
                        cfg->Write(PciConfig::kCommand, command | PCIE_CFG_COMMAND_INT_DISABLE);
                    }
                }
            } else {
                TRACEF("Received legacy PCI INT (system IRQ %u) for %02x:%02x.%02x , but no irq_ "
                       "handlers have been allocated!  Force disabling the interrupt.\n",
                       irq_id_, dev->bus_id_, dev->dev_id_, dev->func_id_);

                {
                    AutoSpinLock cmd_reg_lock(dev->cmd_reg_lock_);
                    command = cfg->Read(PciConfig::kCommand);
                    cfg->Write(PciConfig::kCommand, command | PCIE_CFG_COMMAND_INT_DISABLE);
                }
            }
        }
    }

    return need_resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

void SharedLegacyIrqHandler::AddDevice(PcieDevice& dev) {
    DEBUG_ASSERT(dev.irq_.legacy.shared_handler.get() == this);
    DEBUG_ASSERT(!list_in_list(&dev.irq_.legacy.shared_handler_node));

    /* Make certain that the device's legacy IRQ has been masked at the PCI
     * device level.  Then add this dev to the handler's list.  If this was the
     * first device added to the handler list, unmask the handler IRQ at the top
     * level. */
    AutoSpinLockIrqSave lock(device_handler_list_lock_);

    dev.cfg_->Write(PciConfig::kCommand, dev.cfg_->Read(PciConfig::kCommand) |
                                          PCIE_CFG_COMMAND_INT_DISABLE);

    bool first_device = list_is_empty(&device_handler_list_);
    list_add_tail(&device_handler_list_, &dev.irq_.legacy.shared_handler_node);

    if (first_device)
        unmask_interrupt(irq_id_);
}

void SharedLegacyIrqHandler::RemoveDevice(PcieDevice& dev) {
    DEBUG_ASSERT(dev.irq_.legacy.shared_handler.get() == this);
    DEBUG_ASSERT(list_in_list(&dev.irq_.legacy.shared_handler_node));

    /* Make absolutely sure we have been masked at the PCIe config level, then
     * remove the device from the shared handler list.  If this was the last
     * device on the list, mask the top level IRQ */
    AutoSpinLockIrqSave lock(device_handler_list_lock_);

    dev.cfg_->Write(PciConfig::kCommand, dev.cfg_->Read(PciConfig::kCommand) |
                                          PCIE_CFG_COMMAND_INT_DISABLE);
    list_delete(&dev.irq_.legacy.shared_handler_node);

    if (list_is_empty(&device_handler_list_))
        mask_interrupt(irq_id_);
}

status_t PcieDevice::MaskUnmaskLegacyIrq(bool mask) {
    if (!irq_.handlers || !irq_.handler_count)
        return ERR_INVALID_ARGS;

    pcie_irq_handler_state_t& hstate = irq_.handlers[0];

    {
        AutoSpinLockIrqSave lock(hstate.lock);

        if (mask) ModifyCmdLocked(0, PCIE_CFG_COMMAND_INT_DISABLE);
        else      ModifyCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE, 0);
        hstate.masked = mask;
    }

    return NO_ERROR;
}

status_t PcieDevice::EnterLegacyIrqMode(uint requested_irqs) {
    DEBUG_ASSERT(requested_irqs);

    if (!irq_.legacy.pin || (requested_irqs > 1))
        return ERR_NOT_SUPPORTED;

    // Make absolutely certain we are masked.
    ModifyCmdLocked(0, PCIE_CFG_COMMAND_INT_DISABLE);

    // We can never fail to allocated a single handlers (since we are going to
    // use the pre-allocated singleton)
    __UNUSED status_t res = AllocIrqHandlers(requested_irqs, true);
    DEBUG_ASSERT(res == NO_ERROR);
    DEBUG_ASSERT(irq_.handlers == &irq_.singleton_handler);

    irq_.mode = PCIE_IRQ_MODE_LEGACY;
    irq_.legacy.shared_handler->AddDevice(*this);

    return NO_ERROR;
}

void PcieDevice::LeaveLegacyIrqMode() {
    /* Disable legacy IRQs and unregister from the shared legacy handler */
    MaskUnmaskLegacyIrq(true);
    irq_.legacy.shared_handler->RemoveDevice(*this);

    /* Release any handler storage and reset all of our bookkeeping */
    ResetCommonIrqBookkeeping();
}

/******************************************************************************
 *
 * MSI IRQ mode routines.
 *
 ******************************************************************************/
bool PcieDevice::MaskUnmaskMsiIrqLocked(uint irq_id, bool mask) {
    DEBUG_ASSERT(irq_.mode == PCIE_IRQ_MODE_MSI);
    DEBUG_ASSERT(irq_id < irq_.handler_count);
    DEBUG_ASSERT(irq_.handlers);

    pcie_irq_handler_state_t& hstate = irq_.handlers[irq_id];
    DEBUG_ASSERT(hstate.lock.IsHeld());

    /* Internal code should not be calling this function if they want to mask
     * the interrupt, but it is not possible to do so. */
    DEBUG_ASSERT(!mask ||
                 bus_drv_.platform().supports_msi_masking() ||
                 irq_.msi->has_pvm());

    /* If we can mask at the PCI device level, do so. */
    if (irq_.msi->has_pvm()) {
        DEBUG_ASSERT(irq_id < PCIE_MAX_MSI_IRQS);
        uint32_t  val  = cfg_->Read(irq_.msi->mask_bits_reg());
        if (mask) val |=  (static_cast<uint32_t>(1u) << irq_id);
        else      val &= ~(static_cast<uint32_t>(1u) << irq_id);
        cfg_->Write(irq_.msi->mask_bits_reg(), val);
    }

    /* If we can mask at the platform interrupt controller level, do so. */
    DEBUG_ASSERT(irq_.msi->irq_block_.allocated);
    DEBUG_ASSERT(irq_id < irq_.msi->irq_block_.num_irq);
    if (bus_drv_.platform().supports_msi_masking())
        bus_drv_.platform().MaskUnmaskMsi(&irq_.msi->irq_block_, irq_id, mask);

    bool ret = hstate.masked;
    hstate.masked = mask;
    return ret;
}

status_t PcieDevice::MaskUnmaskMsiIrq(uint irq_id, bool mask) {
    if (irq_id >= irq_.handler_count)
        return ERR_INVALID_ARGS;

    /* If a mask is being requested, and we cannot mask at either the platform
     * interrupt controller or the PCI device level, tell the caller that the
     * operation is unsupported. */
    if (mask && !bus_drv_.platform().supports_msi_masking() && !irq_.msi->has_pvm())
        return ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(irq_.handlers);

    {
        AutoSpinLockIrqSave handler_lock(irq_.handlers[irq_id].lock);
        MaskUnmaskMsiIrqLocked(irq_id, mask);
    }

    return NO_ERROR;
}

void PcieDevice::MaskAllMsiVectors() {
    DEBUG_ASSERT(irq_.msi);
    DEBUG_ASSERT(irq_.msi->is_valid());

    for (uint i = 0; i < irq_.handler_count; i++)
        MaskUnmaskMsiIrq(i, true);

    /* In theory, this should not be needed as all of the relevant bits should
     * have already been masked during the calls to MaskUnmaskMsiIrq.  Just to
     * be careful, however, we explicitly mask all of the upper bits as well. */
    if (irq_.msi->has_pvm())
        cfg_->Write(irq_.msi->mask_bits_reg(), 0xFFFFFFFF);
}

void PcieDevice::SetMsiTarget(uint64_t tgt_addr, uint32_t tgt_data) {
    DEBUG_ASSERT(irq_.msi);
    DEBUG_ASSERT(irq_.msi->is_valid());
    DEBUG_ASSERT(irq_.msi->is64Bit() || !(tgt_addr >> 32));
    DEBUG_ASSERT(!(tgt_data >> 16));

    /* Make sure MSI is disabled_ and all vectors masked (if possible) before
     * changing the target address and data. */
    SetMsiEnb(false);
    MaskAllMsiVectors();

    /* lower bits of the address register are common to all forms of the MSI
     * capability structure.  Upper address bits and data position depend on
     * whether this is a 64 bit or 32 bit version */
    cfg_->Write(irq_.msi->addr_reg(), static_cast<uint32_t>(tgt_addr & 0xFFFFFFFF));
    if (irq_.msi->is64Bit()) {
        cfg_->Write(irq_.msi->addr_upper_reg(), static_cast<uint32_t>(tgt_addr >> 32));
    }
    cfg_->Write(irq_.msi->data_reg(), static_cast<uint16_t>(tgt_data & 0xFFFF));
}

void PcieDevice::FreeMsiBlock() {
    /* If no block has been allocated, there is nothing to do */
    if (!irq_.msi->irq_block_.allocated)
        return;

    DEBUG_ASSERT(bus_drv_.platform().supports_msi());

    /* Mask the IRQ at the platform interrupt controller level if we can, and
     * unregister any registered handler. */
    const pcie_msi_block_t* b = &irq_.msi->irq_block_;
    for (uint i = 0; i < b->num_irq; i++) {
        if (bus_drv_.platform().supports_msi_masking()) {
            bus_drv_.platform().MaskUnmaskMsi(b, i, true);
        }
        bus_drv_.platform().RegisterMsiHandler(b, i, nullptr, nullptr);
    }

    /* Give the block of IRQs back to the plaform */
    bus_drv_.platform().FreeMsiBlock(&irq_.msi->irq_block_);
    DEBUG_ASSERT(!irq_.msi->irq_block_.allocated);
}

void PcieDevice::SetMsiMultiMessageEnb(uint requested_irqs) {
    DEBUG_ASSERT(irq_.msi);
    DEBUG_ASSERT(irq_.msi->is_valid());
    DEBUG_ASSERT((requested_irqs >= 1) && (requested_irqs <= PCIE_MAX_MSI_IRQS));

    uint log2 = log2_uint_ceil(requested_irqs);

    DEBUG_ASSERT(log2 <= 5);
    DEBUG_ASSERT(!log2 || ((0x1u << (log2 - 1)) < requested_irqs));
    DEBUG_ASSERT((0x1u << log2) >= requested_irqs);

    cfg_->Write(irq_.msi->ctrl_reg(),
            PCIE_CAP_MSI_CTRL_SET_MME(log2, cfg_->Read(irq_.msi->ctrl_reg())));
}

void PcieDevice::LeaveMsiIrqMode() {
    /* Disable MSI, mask all vectors and zero out the target */
    SetMsiTarget(0x0, 0x0);

    /* Return any allocated irq_ block to the platform, unregistering with
     * the interrupt controller and synchronizing with the dispatchers in
     * the process. */
    FreeMsiBlock();

    /* Reset our common state, free any allocated handlers */
    ResetCommonIrqBookkeeping();
}

status_t PcieDevice::EnterMsiIrqMode(uint requested_irqs) {
    DEBUG_ASSERT(requested_irqs);

    status_t res = NO_ERROR;

    // We cannot go into MSI mode if we don't support MSI at all, or we don't
    // support the number of IRQs requested
    if (!irq_.msi                             ||
        !irq_.msi->is_valid()                 ||
        !bus_drv_.platform().supports_msi()   ||
        (requested_irqs > irq_.msi->max_irqs()))
        return ERR_NOT_SUPPORTED;

    // If we support PVM, make sure that we are completely masked before
    // attempting to allocte the block of IRQs.
    bool initially_masked;
    if (irq_.msi->has_pvm()) {
        cfg_->Write(irq_.msi->mask_bits_reg(), 0xFFFFFFFF);
        initially_masked = true;
    } else {
        // If we cannot mask at the PCI level, then our IRQs will be initially
        // masked only if the platform supports masking at the interrupt
        // controller level.
        initially_masked = bus_drv_.platform().supports_msi_masking();
    }

    /* Ask the platform for a chunk of MSI compatible IRQs */
    DEBUG_ASSERT(!irq_.msi->irq_block_.allocated);
    res = bus_drv_.platform().AllocMsiBlock(requested_irqs,
                                            irq_.msi->is64Bit(),
                                            false,  /* is_msix == false */
                                            &irq_.msi->irq_block_);
    if (res != NO_ERROR) {
        LTRACEF("Failed to allocate a block of %u MSI IRQs for device "
                "%02x:%02x.%01x (res %d)\n",
                requested_irqs, bus_id_, dev_id_, func_id_, res);
        goto bailout;
    }

    /* Allocate our handler table */
    res = AllocIrqHandlers(requested_irqs, initially_masked);
    if (res != NO_ERROR)
        goto bailout;

    /* Record our new IRQ mode */
    irq_.mode = PCIE_IRQ_MODE_MSI;

    /* Program the target write transaction into the MSI registers.  As a side
     * effect, this will ensure that...
     *
     * 1) MSI mode has been disabled_ at the top level
     * 2) Each IRQ has been masked at system level (if supported)
     * 3) Each IRQ has been masked at the PCI PVM level (if supported)
     */
    DEBUG_ASSERT(irq_.msi->irq_block_.allocated);
    SetMsiTarget(irq_.msi->irq_block_.tgt_addr, irq_.msi->irq_block_.tgt_data);

    /* Properly program the multi-message enable field in the control register */
    SetMsiMultiMessageEnb(requested_irqs);

    /* Register each IRQ with the dispatcher */
    DEBUG_ASSERT(irq_.handler_count <= irq_.msi->irq_block_.num_irq);
    for (uint i = 0; i < irq_.handler_count; ++i) {
        bus_drv_.platform().RegisterMsiHandler(&irq_.msi->irq_block_,
                                               i,
                                               PcieDevice::MsiIrqHandlerThunk,
                                               irq_.handlers + i);
    }

    /* Enable MSI at the top level */
    SetMsiEnb(true);

bailout:
    if (res != NO_ERROR)
        LeaveMsiIrqMode();

    return res;
}

enum handler_return PcieDevice::MsiIrqHandler(pcie_irq_handler_state_t& hstate) {
    DEBUG_ASSERT(irq_.msi);
    /* No need to save IRQ state; we are in an IRQ handler at the moment. */
    AutoSpinLock handler_lock(hstate.lock);

    /* Mask our IRQ if we can. */
    bool was_masked;
    if (bus_drv_.platform().supports_msi_masking() || irq_.msi->has_pvm()) {
        was_masked = MaskUnmaskMsiIrqLocked(hstate.pci_irq_id, true);
    } else {
        DEBUG_ASSERT(!hstate.masked);
        was_masked = false;
    }

    /* If the IRQ was masked or the handler removed by the time we got here,
     * leave the IRQ masked, unlock and get out. */
    if (was_masked || !hstate.handler)
        return INT_NO_RESCHEDULE;

    /* Dispatch */
    pcie_irq_handler_retval_t irq_ret = hstate.handler(*this, hstate.pci_irq_id, hstate.ctx);

    /* Re-enable the IRQ if asked to do so */
    if (!(irq_ret & PCIE_IRQRET_MASK))
        MaskUnmaskMsiIrqLocked(hstate.pci_irq_id, false);

    /* Request a reschedule if asked to do so */
    return (irq_ret & PCIE_IRQRET_RESCHED) ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

enum handler_return PcieDevice::MsiIrqHandlerThunk(void *arg) {
    DEBUG_ASSERT(arg);
    auto& hstate = *(reinterpret_cast<pcie_irq_handler_state_t*>(arg));
    DEBUG_ASSERT(hstate.dev);
    return hstate.dev->MsiIrqHandler(hstate);
}

/******************************************************************************
 *
 * Internal implementation of the Kernel facing API.
 *
 ******************************************************************************/
status_t PcieDevice::QueryIrqModeCapabilitiesLocked(pcie_irq_mode_t mode,
                                                    pcie_irq_mode_caps_t* out_caps) const {
    DEBUG_ASSERT(plugged_in_);
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(out_caps);

    memset(out_caps, 0, sizeof(*out_caps));

    switch (mode) {
    // All devices always support "DISABLED".  No need to set the max_irqs to
    // zero or the PVM supported flag to false, the memset has taken care of
    // this for us already.
    case PCIE_IRQ_MODE_DISABLED:
        return NO_ERROR;

    case PCIE_IRQ_MODE_LEGACY:
        if (!irq_.legacy.pin)
            return ERR_NOT_SUPPORTED;

        out_caps->max_irqs = 1;
        out_caps->per_vector_masking_supported = true;
        break;

    case PCIE_IRQ_MODE_MSI:
        /* If the platform does not support MSI, then we don't support MSI,
         * even if the device does. */
        if (!bus_drv_.platform().supports_msi())
            return ERR_NOT_SUPPORTED;

        /* If the device supports MSI, it will have a pointer to the control
         * structure in config. */
        if (!irq_.msi || !irq_.msi->is_valid())
            return ERR_NOT_SUPPORTED;

        /* We support PVM if either the device does, or if the platform is
         * capable of masking and unmasking individual IRQs from an MSI block
         * allocation. */
        out_caps->max_irqs = irq_.msi->max_irqs();
        out_caps->per_vector_masking_supported = irq_.msi->has_pvm()
                                              || (bus_drv_.platform().supports_msi_masking());
        break;

    case PCIE_IRQ_MODE_MSI_X:
        /* If the platform does not support MSI, then we don't support MSI,
         * even if the device does. */
        if (!bus_drv_.platform().supports_msi())
            return ERR_NOT_SUPPORTED;

        /* TODO(johngro) : finish MSI-X implementation. */
        return ERR_NOT_SUPPORTED;

    default:
        return ERR_INVALID_ARGS;
    }

    return NO_ERROR;
}

status_t PcieDevice::GetIrqModeLocked(pcie_irq_mode_info_t* out_info) const {
    DEBUG_ASSERT(plugged_in_);
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(out_info);

    out_info->mode                = irq_.mode;
    out_info->max_handlers        = irq_.handler_count;
    out_info->registered_handlers = irq_.registered_handler_count;

    return NO_ERROR;
}

status_t PcieDevice::SetIrqModeLocked(pcie_irq_mode_t mode, uint requested_irqs) {
    DEBUG_ASSERT(plugged_in_);
    DEBUG_ASSERT(dev_lock_.IsHeld());

    /* Are we disabling IRQs? */
    if (mode == PCIE_IRQ_MODE_DISABLED) {
        /* If so, and we are already disabled_, cool!  Run some sanity checks and we are done */
        if (irq_.mode == PCIE_IRQ_MODE_DISABLED) {
            DEBUG_ASSERT(!irq_.handlers);
            DEBUG_ASSERT(!irq_.handler_count);
            return NO_ERROR;
        }

        DEBUG_ASSERT(irq_.handlers);
        DEBUG_ASSERT(irq_.handler_count);

        switch (irq_.mode) {
        case PCIE_IRQ_MODE_LEGACY:
            DEBUG_ASSERT(list_in_list(&irq_.legacy.shared_handler_node));

            LeaveLegacyIrqMode();

            DEBUG_ASSERT(!irq_.registered_handler_count);
            return NO_ERROR;

        case PCIE_IRQ_MODE_MSI:
            DEBUG_ASSERT(irq_.msi);
            DEBUG_ASSERT(irq_.msi->is_valid());
            DEBUG_ASSERT(irq_.msi->irq_block_.allocated);

            LeaveMsiIrqMode();

            DEBUG_ASSERT(!irq_.registered_handler_count);
            return NO_ERROR;

        /* Right now, there should be no way to get into MSI-X mode */
        case PCIE_IRQ_MODE_MSI_X:
            DEBUG_ASSERT(false);
            return ERR_NOT_SUPPORTED;

        default:
            /* mode is not one of the valid enum values, this should be impossible */
            DEBUG_ASSERT(false);
            return ERR_INTERNAL;
        }
    }

    /* We are picking an active IRQ mode, sanity check the args */
    if (requested_irqs < 1)
        return ERR_INVALID_ARGS;

    /* If we are picking an active IRQ mode, we need to currently be in the
     * disabled_ state */
    if (irq_.mode != PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    switch (mode) {
    case PCIE_IRQ_MODE_LEGACY: return EnterLegacyIrqMode(requested_irqs);
    case PCIE_IRQ_MODE_MSI:    return EnterMsiIrqMode   (requested_irqs);
    case PCIE_IRQ_MODE_MSI_X:  return ERR_NOT_SUPPORTED;
    default:                   return ERR_INVALID_ARGS;
    }
}

status_t PcieDevice::RegisterIrqHandlerLocked(uint irq_id,
                                              pcie_irq_handler_fn_t handler,
                                              void* ctx) {
    DEBUG_ASSERT(plugged_in_);
    DEBUG_ASSERT(dev_lock_.IsHeld());

    /* Cannot register a handler if we are currently disabled_ */
    if (irq_.mode == PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(irq_.handlers);
    DEBUG_ASSERT(irq_.handler_count);

    /* Make sure that the IRQ ID is within range */
    if (irq_id >= irq_.handler_count)
        return ERR_INVALID_ARGS;

    /* Looks good, register (or unregister the handler) and we are done. */
    pcie_irq_handler_state_t& hstate = irq_.handlers[irq_id];

    /* Update our registered handler bookkeeping.  Perform some sanity checks as we do so */
    if (hstate.handler) {
        DEBUG_ASSERT(irq_.registered_handler_count);
        if (!handler)
            irq_.registered_handler_count--;
    } else {
        if (handler)
            irq_.registered_handler_count++;
    }
    DEBUG_ASSERT(irq_.registered_handler_count <= irq_.handler_count);

    {
        AutoSpinLockIrqSave handler_lock(hstate.lock);
        hstate.handler = handler;
        hstate.ctx     = handler ? ctx : nullptr;
    }

    return NO_ERROR;
}

status_t PcieDevice::MaskUnmaskIrqLocked(uint irq_id, bool mask) {
    DEBUG_ASSERT(plugged_in_);
    DEBUG_ASSERT(dev_lock_.IsHeld());

    /* Cannot manipulate mask status while in the DISABLED state */
    if (irq_.mode == PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(irq_.handlers);
    DEBUG_ASSERT(irq_.handler_count);

    /* Make sure that the IRQ ID is within range */
    if (irq_id >= irq_.handler_count)
        return ERR_INVALID_ARGS;

    /* If we are unmasking (enabling), then we need to make sure that there is a
     * handler in place for the IRQ we are enabling. */
    pcie_irq_handler_state_t& hstate = irq_.handlers[irq_id];
    if (!mask && !hstate.handler)
        return ERR_BAD_STATE;

    /* OK, everything looks good.  Go ahead and make the change based on the
     * mode we are curently in. */
    switch (irq_.mode) {
    case PCIE_IRQ_MODE_LEGACY: return MaskUnmaskLegacyIrq(mask);
    case PCIE_IRQ_MODE_MSI:    return MaskUnmaskMsiIrq(irq_id, mask);
    case PCIE_IRQ_MODE_MSI_X:  return ERR_NOT_SUPPORTED;
    default:
        DEBUG_ASSERT(false); /* This should be un-possible! */
        return ERR_INTERNAL;
    }

    return NO_ERROR;
}

/******************************************************************************
 *
 * Kernel API; prototypes in dev/pcie_irqs.h
 *
 ******************************************************************************/
status_t PcieDevice::QueryIrqModeCapabilities(pcie_irq_mode_t mode,
                                              pcie_irq_mode_caps_t* out_caps) const {
    if (!out_caps)
        return ERR_INVALID_ARGS;

    AutoLock dev_lock(&dev_lock_);

    return (plugged_in_ && !disabled_)
        ? QueryIrqModeCapabilitiesLocked(mode, out_caps)
        : ERR_BAD_STATE;
}

status_t PcieDevice::GetIrqMode(pcie_irq_mode_info_t* out_info) const {
    if (!out_info)
        return ERR_INVALID_ARGS;

    AutoLock dev_lock(&dev_lock_);

    return (plugged_in_ && !disabled_)
        ? GetIrqModeLocked(out_info)
        : ERR_BAD_STATE;
}

status_t PcieDevice::SetIrqMode(pcie_irq_mode_t mode, uint requested_irqs) {
    AutoLock dev_lock(&dev_lock_);

    return ((mode == PCIE_IRQ_MODE_DISABLED) || (plugged_in_ && !disabled_))
        ? SetIrqModeLocked(mode, requested_irqs)
        : ERR_BAD_STATE;
}

status_t PcieDevice::RegisterIrqHandler(uint irq_id, pcie_irq_handler_fn_t handler, void* ctx) {
    AutoLock dev_lock(&dev_lock_);

    return (plugged_in_ && !disabled_)
        ? RegisterIrqHandlerLocked(irq_id, handler, ctx)
        : ERR_BAD_STATE;
}

status_t PcieDevice::MaskUnmaskIrq(uint irq_id, bool mask) {
    AutoLock dev_lock(&dev_lock_);

    return (mask || (plugged_in_ && !disabled_))
        ? MaskUnmaskIrqLocked(irq_id, mask)
        : ERR_BAD_STATE;
}


// Map from a device's interrupt pin ID to the proper system IRQ ID.  Follow the
// PCIe graph up to the root, swizzling as we traverse PCIe switches,
// PCIe-to-PCI bridges, and native PCI-to-PCI bridges.  Once we hit the root,
// perform the final remapping using the platform supplied remapping routine.
//
// Platform independent swizzling behavior is documented in the PCIe base
// specification in section 2.2.8.1 and Table 2-20.
//
// Platform dependent remapping is an exercise for the reader.  FWIW: PC
// architectures use the _PRT tables in ACPI to perform the remapping.
//
status_t PcieDevice::MapPinToIrqLocked(mxtl::RefPtr<PcieUpstreamNode>&& upstream) {
    DEBUG_ASSERT(dev_lock_.IsHeld());

    if (!legacy_irq_pin() || (legacy_irq_pin() > PCIE_MAX_LEGACY_IRQ_PINS))
        return ERR_BAD_STATE;

    auto dev = mxtl::WrapRefPtr(this);
    uint pin = legacy_irq_pin() - 1;  // Change to 0s indexing

    // Walk up the PCI/PCIe tree, applying the swizzling rules as we go.  Stop
    // when we reach the device which is hanging off of the root bus/root
    // complex.  At this point, platform specific swizzling takes over.
    while ((upstream != nullptr) &&
           (upstream->type() == PcieUpstreamNode::Type::BRIDGE)) {
        // TODO(johngro) : Eliminate the null-check of bridge below.  Currently,
        // it is needed because we have gcc/g++'s "null-dereference" warning
        // turned on, and because of the potentially offsetting nature of static
        // casting, the compiler cannot be sure that bridge is non-null, just
        // because upstream was non-null (check in the while predicate, above).
        // Even adding explicit checks to the Downcast method in RefPtr<> does
        // not seem to satisfy it.
        //
        // Some potential future options include...
        // 1) Change this to DEBUG_ASSERT and turn off the null-dereference
        //    warning in release builds.
        // 2) Wait until GCC becomes smart enough to figure this out.
        // 3) Switch completely to clang (assuming that clang does not have
        //    similar problems).
        auto bridge = mxtl::RefPtr<PcieBridge>::Downcast(mxtl::move(upstream));
        if (bridge == nullptr)
            return ERR_INTERNAL;

        // We need to swizzle every time we pass through...
        // 1) A PCI-to-PCI bridge (real or virtual)
        // 2) A PCIe-to-PCI bridge
        // 3) The Upstream port of a switch.
        //
        // We do NOT swizzle when we pass through...
        // 1) A root port hanging off the root complex. (any swizzling here is up
        //    to the platform implementation)
        // 2) A Downstream switch port.  Since downstream PCIe switch ports are
        //    only permitted to have a single device located at position 0 on
        //    their "bus", it does not really matter if we do the swizzle or
        //    not, since it would turn out to be an identity transformation
        //    anyway.
        switch (bridge->pcie_device_type()) {
            // UNKNOWN devices are devices which did not have a PCI Express
            // Capabilities structure in their capabilities list.  Since every
            // device we pass through on the way up the tree should be a device
            // with a Type 1 header, these should be PCI-to-PCI bridges (real or
            // virtual)
            case PCIE_DEVTYPE_UNKNOWN:
            case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:
            case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:
            case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:
                pin = (pin + dev->dev_id()) % PCIE_MAX_LEGACY_IRQ_PINS;
                break;

            default:
                break;
        }

        // Climb one branch higher up the tree
        dev = mxtl::move(bridge);
        upstream = dev->GetUpstream();
    }

    // If our upstream is ever null as we climb the tree, then something must
    // have been unplugged as we were climbing.
    if (upstream == nullptr)
        return ERR_BAD_STATE;

    // We have hit root of the tree.  Something is very wrong if our
    // UpstreamNode is not, in fact, a root.
    if (upstream->type() != PcieUpstreamNode::Type::ROOT) {
        TRACEF("Failed to map legacy pin to platform IRQ ID for dev "
               "%02x:%02x.%01x (pin %u).  Top of the device tree "
               "(managed bus ID 0x%02x) does not appear to be either a root or a "
               "bridge! (type %u)\n",
               bus_id_, dev_id_, func_id_, irq_.legacy.pin,
               upstream->managed_bus_id(), static_cast<uint>(upstream->type()));
        return ERR_BAD_STATE;
    }

    // TODO(johngro) : Eliminate the null-check of root below.  See the TODO for
    // the downcast of upstream -> bridge above for details.
    auto root = mxtl::RefPtr<PcieRoot>::Downcast(mxtl::move(upstream));
    if (root == nullptr)
        return ERR_INTERNAL;
    return root->Swizzle(dev->dev_id(), dev->func_id(), pin, &irq_.legacy.irq_id);
}

status_t PcieDevice::InitLegacyIrqStateLocked(PcieUpstreamNode& upstream) {
    DEBUG_ASSERT(dev_lock_.IsHeld());
    DEBUG_ASSERT(cfg_);
    DEBUG_ASSERT(irq_.legacy.shared_handler == nullptr);

    // Make certain that the device's legacy IRQ (if any) has been disabled.
    ModifyCmdLocked(0u, PCIE_CFG_COMMAND_INT_DISABLE);

    // Does config say that we have a legacy IRQ pin?  If so use the bus driver
    // to map it to the system IRQ ID, then grab a hold of the shared legacy IRQ
    // handler.
    irq_.legacy.pin = cfg_->Read(PciConfig::kInterruptPin);
    if (irq_.legacy.pin) {
        status_t res = MapPinToIrqLocked(mxtl::RefPtr<PcieUpstreamNode>(&upstream));
        if (res != NO_ERROR) {
            TRACEF("Failed to map legacy pin to platform IRQ ID for "
                   "dev %02x:%02x.%01x (pin %u)\n",
                   bus_id_, dev_id_, func_id_,
                   irq_.legacy.pin);
            return res;
        }

        irq_.legacy.shared_handler = bus_drv_.FindLegacyIrqHandler(irq_.legacy.irq_id);
        if (irq_.legacy.shared_handler == nullptr) {
            TRACEF("Failed to find or create shared legacy IRQ handler for "
                   "dev %02x:%02x.%01x (pin %u, irq_id %u)\n",
                   bus_id_, dev_id_, func_id_,
                   irq_.legacy.pin, irq_.legacy.irq_id);
            return ERR_NO_RESOURCES;
        }
    }

    return NO_ERROR;
}

void PcieBusDriver::ShutdownIrqs() {
    /* Shut off all of our legacy IRQs and free all of our bookkeeping */
    AutoLock lock(&legacy_irq_list_lock_);
    legacy_irq_list_.clear();
}

mxtl::RefPtr<SharedLegacyIrqHandler> PcieBusDriver::FindLegacyIrqHandler(uint irq_id) {
    /* Search to see if we have already created a shared handler for this system
     * level IRQ id already */
    AutoLock lock(&legacy_irq_list_lock_);

    auto iter = legacy_irq_list_.begin();
    while (iter != legacy_irq_list_.end()) {
        if (irq_id == iter->irq_id())
            return iter.CopyPointer();
        ++iter;
    }

    auto handler = SharedLegacyIrqHandler::Create(irq_id);
    if (handler != nullptr)
        legacy_irq_list_.push_front(handler);

    return handler;
}
