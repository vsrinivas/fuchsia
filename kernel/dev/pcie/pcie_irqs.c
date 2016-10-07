// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <dev/pcie.h>
#include <err.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>

#include "pcie_priv.h"

#define LOCAL_TRACE 0

/******************************************************************************
 *
 * Helper routines common to all IRQ modes.
 *
 ******************************************************************************/
static void pcie_reset_common_irq_bookkeeping(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);

    if (dev->irq.handler_count > 1) {
        DEBUG_ASSERT(dev->irq.handlers != &dev->irq.singleton_handler);
        free(dev->irq.handlers);
    }

    memset(&dev->irq.singleton_handler, 0, sizeof(dev->irq.singleton_handler));
    dev->irq.mode          = PCIE_IRQ_MODE_DISABLED;
    dev->irq.handlers      = NULL;
    dev->irq.handler_count = 0;
}

static status_t pcie_alloc_irq_handlers(pcie_device_state_t* dev, uint requested_irqs) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(requested_irqs);
    DEBUG_ASSERT(!dev->irq.handlers);
    DEBUG_ASSERT(!dev->irq.handler_count);

    if (requested_irqs == 1) {
        memset(&dev->irq.singleton_handler, 0, sizeof(dev->irq.singleton_handler));
        dev->irq.handlers      = &dev->irq.singleton_handler;
        dev->irq.handler_count = 1;
        goto finish_bookkeeping;
    }

    dev->irq.handlers = calloc(requested_irqs, sizeof(*dev->irq.handlers));
    if (!dev->irq.handlers)
        return ERR_NO_MEMORY;
    dev->irq.handler_count = requested_irqs;

finish_bookkeeping:
    for (uint i = 0; i < dev->irq.handler_count; ++i) {
        pcie_irq_handler_state_t* h = &dev->irq.handlers[i];
        h->dev        = dev;
        h->pci_irq_id = i;
        spin_lock_init(&h->lock);
    }
    return NO_ERROR;
}

/******************************************************************************
 *
 * Legacy IRQ mode routines.
 *
 ******************************************************************************/
static enum handler_return pcie_legacy_irq_handler(void *arg) {
    DEBUG_ASSERT(arg);
    pcie_legacy_irq_handler_state_t* legacy_hstate = (pcie_legacy_irq_handler_state_t*)(arg);
    pcie_bus_driver_state_t*         bus_drv       = legacy_hstate->bus_drv;
    bool need_resched = false;

    /* Go over the list of device's which share this legacy IRQ and give them a
     * chance to handle any interrupts which may be pending in their device.
     * Keep track of whether or not any device has requested a re-schedule event
     * at the end of this IRQ.  Also, if we receive an interrupt for a device
     * who has no registered legacy_hstate, print a warning and disable the IRQ at the
     * PCIe controller level.  With no registered legacy_hstate, we have no way to
     * suppress the IRQ and need to disable it in order to prevent locking up
     * the whole system.
     */
    pcie_device_state_t* dev;
    spin_lock(&bus_drv->legacy_irq_handler_lock);

    if (list_is_empty(&legacy_hstate->device_handler_list)) {
        TRACEF("Received legacy PCI INT (system IRQ %u), but there are no devices registered to "
               "handle this interrupt.  This is Very Bad.  Disabling the interrupt at the system "
               "IRQ level to prevent meltdown.\n",
               legacy_hstate->irq_id);
        mask_interrupt(legacy_hstate->irq_id);
        goto finished;
    }

    list_for_every_entry(&legacy_hstate->device_handler_list,
                         dev,
                         pcie_device_state_t,
                         irq.legacy.shared_handler_node) {
        pcie_config_t* cfg = dev->cfg;

        uint16_t command = pcie_read16(&cfg->base.command);
        uint16_t status  = pcie_read16(&cfg->base.status);

        if ((status & PCIE_CFG_STATUS_INT_STS) && !(command & PCIE_CFG_COMMAND_INT_DISABLE)) {
            DEBUG_ASSERT(dev);
            pcie_irq_handler_state_t* hstate  = &dev->irq.handlers[0];

            if (hstate) {
                pcie_irq_handler_retval_t irq_ret = PCIE_IRQRET_MASK;
                spin_lock(&hstate->lock);

                if (hstate->handler) {
                    if (!hstate->masked)
                        irq_ret = hstate->handler(dev, 0, hstate->ctx);

                    if (irq_ret & PCIE_IRQRET_RESCHED)
                        need_resched = true;
                } else {
                    TRACEF("Received legacy PCI INT (system IRQ %u) for %02x:%02x.%02x (driver "
                           "\"%s\"), but no irq handler has been registered by the driver.  Force "
                           "disabling the interrupt.\n",
                           legacy_hstate->irq_id,
                           dev->bus_id, dev->dev_id, dev->func_id,
                           pcie_driver_name(dev->driver));
                }

                if (irq_ret & PCIE_IRQRET_MASK) {
                    hstate->masked = true;
                    pcie_write16(&cfg->base.command, command | PCIE_CFG_COMMAND_INT_DISABLE);
                }

                spin_unlock(&hstate->lock);
            } else {
                TRACEF("Received legacy PCI INT (system IRQ %u) for %02x:%02x.%02x (driver "
                       "\"%s\"), but no irq handlers have been allocated!  Force "
                       "disabling the interrupt.\n",
                       legacy_hstate->irq_id,
                       dev->bus_id, dev->dev_id, dev->func_id,
                       pcie_driver_name(dev->driver));

                pcie_write16(&cfg->base.command, command | PCIE_CFG_COMMAND_INT_DISABLE);
            }
        }
    }

finished:
    spin_unlock(&bus_drv->legacy_irq_handler_lock);
    return need_resched ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static inline status_t pcie_mask_unmask_legacy_irq(pcie_device_state_t* dev,
                                                   bool                 mask) {

    if (!dev->irq.handlers || !dev->irq.handler_count)
        return ERR_INVALID_ARGS;

    pcie_config_t*            cfg    = dev->cfg;
    pcie_irq_handler_state_t* hstate = &dev->irq.handlers[0];
    spin_lock_saved_state_t   irq_state;
    uint16_t val;

    spin_lock_irqsave(&hstate->lock, irq_state);

    val = pcie_read16(&cfg->base.command);
    if (mask) val |= PCIE_CFG_COMMAND_INT_DISABLE;
    else      val &= ~PCIE_CFG_COMMAND_INT_DISABLE;
    pcie_write16(&cfg->base.command, val);
    hstate->masked = mask;

    spin_unlock_irqrestore(&hstate->lock, irq_state);

    return NO_ERROR;
}

/*
 * Map from a device's interrupt pin ID to the proper system IRQ ID.  Follow the
 * PCIe graph up to the root, swizzling as we traverse PCIe switches,
 * PCIe-to-PCI bridges, and native PCI-to-PCI bridges.  Once we hit the root,
 * perform the final remapping using the platform supplied remapping routine.
 *
 * Platform independent swizzling behavior is documented in the PCIe base
 * specification in section 2.2.8.1 and Table 2-20.
 *
 * Platform dependent remapping is an exercise for the reader.  FWIW: PC
 * architectures use the _PRT tables in ACPI to perform the remapping.
 */
static uint pcie_map_pin_to_irq(pcie_device_state_t* dev, uint pin, pcie_bridge_state_t* upstream) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(pin);
    DEBUG_ASSERT(pin <= PCIE_MAX_LEGACY_IRQ_PINS);

    pin -= 1; // Change to 0s indexing

    /* Hold the bus topology lock while we do this, so we don't need to worry
     * about stuff disappearing as we walk the tree up */
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    MUTEX_ACQUIRE(bus_drv, bus_topology_lock);

    /* Walk up the PCI/PCIe tree, applying the swizzling rules as we go.  Stop
     * when we reach the device which is hanging off of the root bus/root
     * complex.  At this point, platform specific swizzling takes over.
     */
    DEBUG_ASSERT(upstream);  // We should not be mapping IRQs for the "host bridge" special case
    while (upstream->dev.upstream) {
        /* We need to swizzle every time we pass through...
         * 1) A PCI-to-PCI bridge (real or virtual)
         * 2) A PCIe-to-PCI bridge
         * 3) The Upstream port of a switch.
         *
         * We do NOT swizzle when we pass through...
         * 1) A root port hanging off the root complex. (any swizzling here is up
         *    to the platform implementation)
         * 2) A Downstream switch port.  Since downstream PCIe switch ports are
         *    only permitted to have a single device located at position 0 on
         *    their "bus", it does not really matter if we do the swizzle or
         *    not, since it would turn out to be an identity transformation
         *    anyway.
         *
         * TODO(johngro) : Consider removing this logic.  For both of the cases
         * where we traverse a node with a type 1 config header but don't apply
         * the swizzling rules (downstream switch ports and root ports),
         * application of the swizzle operation should be a no-op because the
         * device number of the device hanging off the "secondary bus" should
         * always be zero.  The final step through the root complex, either from
         * integrated endpoint or root port, is left to the system and does not
         * pass through this code.
         */
        switch (upstream->dev.pcie_caps.devtype) {
            /* UNKNOWN devices are devices which did not have a PCI Express
             * Capabilities structure in their capabilities list.  Since every
             * device we pass through on the way up the tree should be a device
             * with a Type 1 header, these should be PCI-to-PCI bridges (real or
             * virtual) */
            case PCIE_DEVTYPE_UNKNOWN:
            case PCIE_DEVTYPE_SWITCH_UPSTREAM_PORT:
            case PCIE_DEVTYPE_PCIE_TO_PCI_BRIDGE:
            case PCIE_DEVTYPE_PCI_TO_PCIE_BRIDGE:
                pin = (pin + dev->dev_id) % PCIE_MAX_LEGACY_IRQ_PINS;
                break;

            default:
                break;
        }

        /* Climb one branch higher up the tree */
        dev      = &upstream->dev;
        upstream = dev->upstream;
    }

    MUTEX_RELEASE(bus_drv, bus_topology_lock);

    uint irq;
    __UNUSED status_t status;
    DEBUG_ASSERT(bus_drv && bus_drv->legacy_irq_swizzle);
    status = bus_drv->legacy_irq_swizzle(dev, pin, &irq);
    DEBUG_ASSERT(status == NO_ERROR);

    return irq;
}

static pcie_legacy_irq_handler_state_t* pcie_find_legacy_irq_handler(
        pcie_bus_driver_state_t* bus_drv,
        uint irq_id) {
    DEBUG_ASSERT(bus_drv);

    /* Search to see if we have already created a shared handler for this system
     * level IRQ id already */
    pcie_legacy_irq_handler_state_t* ret = NULL;
    MUTEX_ACQUIRE(bus_drv, legacy_irq_list_lock);

    list_for_every_entry(&bus_drv->legacy_irq_list,
                         ret,
                         pcie_legacy_irq_handler_state_t,
                         legacy_irq_list_node) {
        if (irq_id == ret->irq_id)
            goto finished;
    }


    /* Looks like we didn't find one.  Allocate and initialize a new entry, then
     * add it to the list. */
    ret = (pcie_legacy_irq_handler_state_t*)calloc(1, sizeof(*ret));
    ASSERT(ret);

    ret->bus_drv    = bus_drv;
    ret->irq_id = irq_id;
    list_initialize(&ret->device_handler_list);
    list_add_tail  (&bus_drv->legacy_irq_list, &ret->legacy_irq_list_node);

    register_int_handler(ret->irq_id, pcie_legacy_irq_handler, (void*)ret);
    unmask_interrupt(ret->irq_id);

finished:
    MUTEX_RELEASE(bus_drv, legacy_irq_list_lock);
    return ret;
}

/* Add this device to the shared legacy IRQ handler assigned to it. */
static void pcie_register_legacy_irq_handler(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.legacy.shared_handler);
    DEBUG_ASSERT(!list_in_list(&dev->irq.legacy.shared_handler_node));

    pcie_bus_driver_state_t*         bus_drv = dev->bus_drv;
    pcie_legacy_irq_handler_state_t* handler = dev->irq.legacy.shared_handler;
    spin_lock_saved_state_t          irq_state;
    DEBUG_ASSERT(bus_drv);

    /* Add this dev to the handler's list. */
    spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);
    list_add_tail(&handler->device_handler_list, &dev->irq.legacy.shared_handler_node);
    spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);
}

/*
 * If this dev is currently registered with its shared legacy IRQ handler,
 * unregister it now.
 */
static void pcie_unregister_legacy_irq_handler(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.legacy.shared_handler);
    DEBUG_ASSERT(list_in_list(&dev->irq.legacy.shared_handler_node));

    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    spin_lock_saved_state_t  irq_state;
    DEBUG_ASSERT(bus_drv);

    /* Make absolutely sure we have been masked at the PCIe config level before
     * removing ourselves from the shared handler list. */
    pcie_write16(&dev->cfg->base.command, pcie_read16(&dev->cfg->base.command) |
                                          PCIE_CFG_COMMAND_INT_DISABLE);

    /* Remove the dev from the shared handler's list */
    spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);
    list_delete(&dev->irq.legacy.shared_handler_node);
    spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);
}

static void pcie_leave_legacy_irq_mode(pcie_device_state_t* dev) {
    /* Disable legacy IRQs and unregister from the shared legacy handler */
    pcie_mask_unmask_legacy_irq(dev, true);
    pcie_unregister_legacy_irq_handler(dev);

    /* Release any handler storage and reset all of our bookkeeping */
    pcie_reset_common_irq_bookkeeping(dev);
}

static status_t pcie_enter_legacy_irq_mode(pcie_device_state_t*    dev,
                                           uint                    requested_irqs) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(requested_irqs);

    if (!dev->irq.legacy.pin || (requested_irqs > 1))
        return ERR_NOT_SUPPORTED;

    /* We can never fail to allocated a single handlers (since we are going to
     * use the pre-allocated singleton) */
    __UNUSED status_t res = pcie_alloc_irq_handlers(dev, requested_irqs);
    DEBUG_ASSERT(res == NO_ERROR);
    DEBUG_ASSERT(dev->irq.handlers == &dev->irq.singleton_handler);

    dev->irq.mode = PCIE_IRQ_MODE_LEGACY;

    pcie_register_legacy_irq_handler(dev);
    return NO_ERROR;
}

/******************************************************************************
 *
 * MSI IRQ mode routines.
 *
 ******************************************************************************/
static inline void pcie_set_msi_enb(pcie_device_state_t* dev, bool enb) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.msi.cfg);

    volatile uint16_t* ctrl_reg = &dev->irq.msi.cfg->ctrl;
    pcie_write16(ctrl_reg, PCIE_CAP_MSI_CTRL_SET_ENB(enb, pcie_read16(ctrl_reg)));
}

static inline bool pcie_mask_unmask_msi_irq_locked(pcie_device_state_t* dev,
                                                   uint                 irq_id,
                                                   bool                 mask) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.mode == PCIE_IRQ_MODE_MSI);
    DEBUG_ASSERT(irq_id < dev->irq.handler_count);
    DEBUG_ASSERT(dev->irq.handlers);

    pcie_bus_driver_state_t*  bus_drv = dev->bus_drv;
    pcie_irq_handler_state_t* hstate  = &dev->irq.handlers[irq_id];
    DEBUG_ASSERT(spin_lock_held(&hstate->lock));

    /* Internal code should not be calling this function if they want to mask
     * the interrupt, but it is not possible to do so. */
    DEBUG_ASSERT(!mask || bus_drv->mask_unmask_msi || dev->irq.msi.pvm_mask_reg);

    /* If we can mask at the PCI device level, do so. */
    if (dev->irq.msi.pvm_mask_reg) {
        DEBUG_ASSERT(irq_id < PCIE_MAX_MSI_IRQS);
        uint32_t  val  = pcie_read32(dev->irq.msi.pvm_mask_reg);
        if (mask) val |=  ((uint32_t)1 << irq_id);
        else      val &= ~((uint32_t)1 << irq_id);
        pcie_write32(dev->irq.msi.pvm_mask_reg, val);
    }


    /* If we can mask at the platform interrupt controller level, do so. */
    DEBUG_ASSERT(dev->irq.msi.irq_block.allocated);
    DEBUG_ASSERT(irq_id < dev->irq.msi.irq_block.num_irq);
    if (bus_drv->mask_unmask_msi)
        bus_drv->mask_unmask_msi(&dev->irq.msi.irq_block, irq_id, mask);

    bool ret = hstate->masked;
    hstate->masked = mask;
    return ret;
}

static inline status_t pcie_mask_unmask_msi_irq(pcie_device_state_t* dev,
                                                uint                 irq_id,
                                                bool                 mask) {
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    spin_lock_saved_state_t irq_state;

    if (irq_id >= dev->irq.handler_count)
        return ERR_INVALID_ARGS;

    /* If a mask is being requested, and we cannot mask at either the platform
     * interrupt controller or the PCI device level, tell the caller that the
     * operation is unsupported. */
    if (mask && !bus_drv->mask_unmask_msi && !dev->irq.msi.pvm_mask_reg)
        return ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(dev->irq.handlers);

    spin_lock_irqsave(&dev->irq.handlers[irq_id].lock, irq_state);
    pcie_mask_unmask_msi_irq_locked(dev, irq_id, mask);
    spin_unlock_irqrestore(&dev->irq.handlers[irq_id].lock, irq_state);

    return NO_ERROR;
}

static void pcie_mask_all_msi_vectors(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.msi.cfg);

    for (uint i = 0; i < dev->irq.handler_count; i++)
        pcie_mask_unmask_msi_irq(dev, i, true);

    /* In theory, this should not be needed as all of the relevant bits should
     * have already been masked during the calls to pcie_mask_unmask_msi_irq.
     * Just to be careful, however, we explicitly mask all of the upper bits as well. */
    if (dev->irq.msi.pvm_mask_reg)
        pcie_write32(dev->irq.msi.pvm_mask_reg, 0xFFFFFFFF);
}

static void pcie_set_msi_target(pcie_device_state_t* dev,
                                uint64_t tgt_addr,
                                uint32_t tgt_data) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.msi.cfg);
    DEBUG_ASSERT(dev->irq.msi.is64bit || !(tgt_addr >> 32));
    DEBUG_ASSERT(!(tgt_data >> 16));

    /* Make sure MSI is disabled and all vectors masked (if possible) before
     * changing the target address and data. */
    pcie_set_msi_enb(dev, false);
    pcie_mask_all_msi_vectors(dev);

    /* lower bits of the address register are common to all forms of the MSI
     * capability structure.  Upper address bits and data position depend on
     * whether this is a 64 bit or 32 bit version */
    pcie_write32(&dev->irq.msi.cfg->addr, (uint32_t)(tgt_addr & 0xFFFFFFFF));
    if (dev->irq.msi.is64bit) {
        pcie_write32(&dev->irq.msi.cfg->nopvm_64bit.addr_upper, (uint32_t)(tgt_addr >> 32));
        pcie_write16(&dev->irq.msi.cfg->nopvm_64bit.data,       (uint16_t)(tgt_data & 0xFFFF));
    } else {
        pcie_write16(&dev->irq.msi.cfg->nopvm_32bit.data,       (uint16_t)(tgt_data & 0xFFFF));
    }
}

static enum handler_return pcie_msi_irq_handler(void *arg) {
    DEBUG_ASSERT(arg);
    pcie_irq_handler_state_t* hstate  = (pcie_irq_handler_state_t*)arg;
    pcie_device_state_t*      dev     = hstate->dev;
    pcie_bus_driver_state_t*  bus_drv = dev->bus_drv;

    /* No need to save IRQ state; we are in an IRQ handler at the moment. */
    DEBUG_ASSERT(hstate);
    spin_lock(&hstate->lock);

    /* Mask our IRQ if we can. */
    bool was_masked;
    if (bus_drv->mask_unmask_msi || dev->irq.msi.pvm_mask_reg) {
        was_masked = pcie_mask_unmask_msi_irq_locked(dev, hstate->pci_irq_id, true);
    } else {
        DEBUG_ASSERT(!hstate->masked);
        was_masked = false;
    }

    /* If the IRQ was masked or the handler removed by the time we got here,
     * leave the IRQ masked, unlock and get out. */
    if (was_masked || !hstate->handler) {
        spin_unlock(&hstate->lock);
        return INT_NO_RESCHEDULE;
    }

    /* Dispatch */
    pcie_irq_handler_retval_t irq_ret = hstate->handler(dev, hstate->pci_irq_id, hstate->ctx);

    /* Re-enable the IRQ if asked to do so */
    if (!(irq_ret & PCIE_IRQRET_MASK))
        pcie_mask_unmask_msi_irq_locked(dev, hstate->pci_irq_id, false);

    /* Unlock and request a reschedule if asked to do so */
    spin_unlock(&hstate->lock);
    return (irq_ret & PCIE_IRQRET_RESCHED) ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

static void pcie_free_msi_block(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;

    /* If no block has been allocated, there is nothing to do */
    if (!dev->irq.msi.irq_block.allocated)
        return;

    DEBUG_ASSERT(bus_drv->register_msi_handler);
    DEBUG_ASSERT(bus_drv->free_msi_block);

    /* Mask the IRQ at the platform interrupt controller level if we can, and
     * unregister any registered handler. */
    const pcie_msi_block_t* b = &dev->irq.msi.irq_block;
    for (uint i = 0; i < b->num_irq; i++) {
        if (bus_drv->mask_unmask_msi)
            bus_drv->mask_unmask_msi(b, i, true);

        bus_drv->register_msi_handler(b, i, NULL, NULL);
    }

    DEBUG_ASSERT(bus_drv);
    DEBUG_ASSERT(bus_drv->free_msi_block);

    /* Give the block of IRQs back to the plaform */
    bus_drv->free_msi_block(&dev->irq.msi.irq_block);
    DEBUG_ASSERT(!dev->irq.msi.irq_block.allocated);
}

static void pcie_set_msi_multi_message_enb(pcie_device_state_t* dev, uint requested_irqs) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->irq.msi.cfg);
    DEBUG_ASSERT((requested_irqs >= 1) && (requested_irqs <= PCIE_MAX_MSI_IRQS));

    uint log2 = log2_uint_ceil(requested_irqs);

    DEBUG_ASSERT(log2 <= 5);
    DEBUG_ASSERT(!log2 || ((0x1u << (log2 - 1)) < requested_irqs));
    DEBUG_ASSERT((0x1u << log2) >= requested_irqs);

    volatile uint16_t* ctrl_reg = &dev->irq.msi.cfg->ctrl;
    pcie_write16(ctrl_reg,
                 PCIE_CAP_MSI_CTRL_SET_MME(log2, pcie_read16(ctrl_reg)));
}

static void pcie_leave_msi_irq_mode(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->bus_drv);

    /* Disable MSI, mask all vectors and zero out the target */
    pcie_set_msi_target(dev, 0x0, 0x0);

    /* Return any allocated irq block to the platform, unregistering with
     * the interrupt controller and synchronizing with the dispatchers in
     * the process. */
    pcie_free_msi_block(dev);

    /* Reset our common state, free any allocated handlers */
    pcie_reset_common_irq_bookkeeping(dev);
}

static status_t pcie_enter_msi_irq_mode(pcie_device_state_t*    dev,
                                        uint                    requested_irqs) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->bus_drv);
    DEBUG_ASSERT(requested_irqs);

    status_t res = NO_ERROR;

    /* We cannot go into MSI mode if we don't support MSI at all, or we
     * don't support the number of IRQs requested */
    if (!dev->irq.msi.cfg               ||
        !dev->bus_drv->alloc_msi_block  ||
        (requested_irqs > dev->irq.msi.max_irqs))
        return ERR_NOT_SUPPORTED;

    DEBUG_ASSERT(dev->bus_drv->free_msi_block &&
                 dev->bus_drv->register_msi_handler);

    /* Ask the platform for a chunk of MSI compatible IRQs */
    DEBUG_ASSERT(!dev->irq.msi.irq_block.allocated);
    res = dev->bus_drv->alloc_msi_block(requested_irqs,
                                        dev->irq.msi.is64bit,
                                        false,  /* is_msix == false */
                                        &dev->irq.msi.irq_block);
    if (res != NO_ERROR) {
        LTRACEF("Failed to allocate a block of %u MSI IRQs for device "
                "%02x:%02x.%01x (res %d)\n",
                requested_irqs, dev->bus_id, dev->dev_id, dev->func_id, res);
        goto bailout;
    }

    /* Allocate our handler table */
    res = pcie_alloc_irq_handlers(dev, requested_irqs);
    if (res != NO_ERROR)
        goto bailout;

    /* Record our new IRQ mode */
    dev->irq.mode       = PCIE_IRQ_MODE_MSI;

    /* Program the target write transaction into the MSI registers.  As a side
     * effect, this will ensure that...
     *
     * 1) MSI mode has been disabled at the top level
     * 2) Each IRQ has been masked at system level (if supported)
     * 3) Each IRQ has been masked at the PCI PVM level (if supported)
     */
    DEBUG_ASSERT(dev->irq.msi.irq_block.allocated);
    pcie_set_msi_target(dev,
                        dev->irq.msi.irq_block.tgt_addr,
                        dev->irq.msi.irq_block.tgt_data);

    /* Properly program the multi-message enable field in the control register */
    pcie_set_msi_multi_message_enb(dev, requested_irqs);

    /* Register each IRQ with the dispatcher */
    DEBUG_ASSERT(dev->irq.handler_count <= dev->irq.msi.irq_block.num_irq);
    for (uint i = 0; i < dev->irq.handler_count; ++i) {
        dev->bus_drv->register_msi_handler(&dev->irq.msi.irq_block,
                                           i,
                                           pcie_msi_irq_handler,
                                           dev->irq.handlers + i);
    }

    /* Enable MSI at the top level */
    pcie_set_msi_enb(dev, true);

bailout:
    if (res != NO_ERROR)
        pcie_leave_msi_irq_mode(dev);

    return res;
}

/******************************************************************************
 *
 * Internal implementation of the Kernel facing API.
 *
 ******************************************************************************/
status_t pcie_query_irq_mode_capabilities_internal(const pcie_device_state_t* dev,
                                                   pcie_irq_mode_t mode,
                                                   pcie_irq_mode_caps_t* out_caps) {
    DEBUG_ASSERT(dev && dev->plugged_in);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));
    DEBUG_ASSERT(dev->bus_drv);
    DEBUG_ASSERT(out_caps);

    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    memset(out_caps, 0, sizeof(*out_caps));

    switch (mode) {
    case PCIE_IRQ_MODE_LEGACY:
        if (!dev->irq.legacy.pin)
            return ERR_NOT_SUPPORTED;

        out_caps->max_irqs = 1;
        out_caps->per_vector_masking_supported = true;
        break;

    case PCIE_IRQ_MODE_MSI:
        /* If the platorm cannot allocate MSI blocks, then we don't support MSI,
         * even if the device does. */
        if (!bus_drv->alloc_msi_block)
            return ERR_NOT_SUPPORTED;

        /* If the device supports MSI, it will have a pointer to the control
         * structure in config. */
        if (!dev->irq.msi.cfg)
            return ERR_NOT_SUPPORTED;

        /* We support PVM if either the device does, or if the platform is
         * capable of masking and unmasking individual IRQs from an MSI block
         * allocation. */
        out_caps->max_irqs = dev->irq.msi.max_irqs;
        out_caps->per_vector_masking_supported = (dev->irq.msi.pvm_mask_reg != NULL)
                                               || (bus_drv->mask_unmask_msi != NULL);
        break;

    case PCIE_IRQ_MODE_MSI_X:
        /* If the platorm cannot allocate MSI blocks, then we don't support
         * MSI-X, even if the device does. */
        if (!bus_drv->alloc_msi_block)
            return ERR_NOT_SUPPORTED;

        /* TODO(johngro) : finish MSI-X implementation. */
        return ERR_NOT_SUPPORTED;

    default:
        return ERR_INVALID_ARGS;
    }

    return NO_ERROR;
}

status_t pcie_get_irq_mode_internal(const struct pcie_device_state* dev,
                                    pcie_irq_mode_info_t* out_info) {
    DEBUG_ASSERT(dev && dev->plugged_in);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));
    DEBUG_ASSERT(out_info);

    out_info->mode                = dev->irq.mode;
    out_info->max_handlers        = dev->irq.handler_count;
    out_info->registered_handlers = dev->irq.registered_handler_count;

    return NO_ERROR;
}

status_t pcie_set_irq_mode_internal(pcie_device_state_t*    dev,
                                    pcie_irq_mode_t         mode,
                                    uint                    requested_irqs) {
    DEBUG_ASSERT(dev && dev->plugged_in);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));

    /* Are we disabling IRQs? */
    if (mode == PCIE_IRQ_MODE_DISABLED) {
        /* If so, and we are already disabled, cool!  Run some sanity checks and we are done */
        if (dev->irq.mode == PCIE_IRQ_MODE_DISABLED) {
            DEBUG_ASSERT(!dev->irq.handlers);
            DEBUG_ASSERT(!dev->irq.handler_count);
            return NO_ERROR;
        }

        DEBUG_ASSERT(dev->irq.handlers);
        DEBUG_ASSERT(dev->irq.handler_count);

        switch (dev->irq.mode) {
        case PCIE_IRQ_MODE_LEGACY:
            DEBUG_ASSERT(list_in_list(&dev->irq.legacy.shared_handler_node));

            pcie_leave_legacy_irq_mode(dev);

            DEBUG_ASSERT(!dev->irq.registered_handler_count);
            return NO_ERROR;

        case PCIE_IRQ_MODE_MSI:
            DEBUG_ASSERT(dev->irq.msi.cfg);
            DEBUG_ASSERT(dev->irq.msi.irq_block.allocated);

            pcie_leave_msi_irq_mode(dev);

            DEBUG_ASSERT(!dev->irq.registered_handler_count);
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
     * disabled state */
    if (dev->irq.mode != PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    switch (mode) {
    case PCIE_IRQ_MODE_LEGACY: return pcie_enter_legacy_irq_mode(dev, requested_irqs);
    case PCIE_IRQ_MODE_MSI:    return pcie_enter_msi_irq_mode   (dev, requested_irqs);
    case PCIE_IRQ_MODE_MSI_X:  return ERR_NOT_SUPPORTED;
    default:                   return ERR_INVALID_ARGS;
    }
}

status_t pcie_register_irq_handler_internal(pcie_device_state_t*  dev,
                                            uint                  irq_id,
                                            pcie_irq_handler_fn_t handler,
                                            void*                 ctx) {
    DEBUG_ASSERT(dev && dev->plugged_in);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));

    /* Cannot register a handler if we are currently disabled */
    if (dev->irq.mode == PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(dev->irq.handlers);
    DEBUG_ASSERT(dev->irq.handler_count);

    /* Make sure that the IRQ ID is within range */
    if (irq_id >= dev->irq.handler_count)
        return ERR_INVALID_ARGS;

    /* Looks good, register (or unregister the handler) and we are done. */
    spin_lock_saved_state_t irq_state;
    pcie_irq_handler_state_t* hstate = &dev->irq.handlers[irq_id];

    /* Update our registered handler bookkeeping.  Perform some sanity checks as we do so */
    if (hstate->handler) {
        DEBUG_ASSERT(dev->irq.registered_handler_count);
        if (!handler)
            dev->irq.registered_handler_count--;
    } else {
        if (handler)
            dev->irq.registered_handler_count++;
    }
    DEBUG_ASSERT(dev->irq.registered_handler_count <= dev->irq.handler_count);

    spin_lock_irqsave(&hstate->lock, irq_state);
    hstate->handler = handler;
    hstate->ctx     = handler ? ctx : NULL;
    spin_unlock_irqrestore(&hstate->lock, irq_state);

    return NO_ERROR;
}

status_t pcie_mask_unmask_irq_internal(pcie_device_state_t* dev,
                                       uint                 irq_id,
                                       bool                 mask) {
    DEBUG_ASSERT(dev && dev->plugged_in);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));

    /* Cannot manipulate mask status while in the DISABLED state */
    if (dev->irq.mode == PCIE_IRQ_MODE_DISABLED)
        return ERR_BAD_STATE;

    DEBUG_ASSERT(dev->irq.handlers);
    DEBUG_ASSERT(dev->irq.handler_count);

    /* Make sure that the IRQ ID is within range */
    if (irq_id >= dev->irq.handler_count)
        return ERR_INVALID_ARGS;

    /* If we are unmasking (enabling), then we need to make sure that there is a
     * handler in place for the IRQ we are enabling. */
    pcie_irq_handler_state_t* hstate = &dev->irq.handlers[irq_id];
    if (!mask && !hstate->handler)
        return ERR_BAD_STATE;

    /* OK, everything looks good.  Go ahead and make the change based on the
     * mode we are curently in. */
    switch (dev->irq.mode) {
    case PCIE_IRQ_MODE_LEGACY: return pcie_mask_unmask_legacy_irq(dev, mask);
    case PCIE_IRQ_MODE_MSI:    return pcie_mask_unmask_msi_irq(dev, irq_id, mask);
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
status_t pcie_query_irq_mode_capabilities(const pcie_device_state_t* dev,
                                          pcie_irq_mode_t mode,
                                          pcie_irq_mode_caps_t* out_caps) {
    DEBUG_ASSERT(dev);
    if (!out_caps)
        return ERR_INVALID_ARGS;

    status_t ret;

    // TODO(johngro) : Is casting away this const evil?  Yes.  Until we switch
    // to C++, however, I cannot flag the lock member as mutable.  Since
    // pcie_device_state_t's are never going to live in an un-modifiable page,
    // I'd rather leave the API as const, and cast away the const in order to
    // obtain the lock.  When things move to C++, we can solve the problem with
    // mutable.
    MUTEX_ACQUIRE(((pcie_device_state_t*)dev), dev_lock);
    ret = dev->plugged_in
        ? pcie_query_irq_mode_capabilities_internal(dev, mode, out_caps)
        : ERR_BAD_STATE;
    MUTEX_RELEASE(((pcie_device_state_t*)dev), dev_lock);

    return ret;
}

status_t pcie_get_irq_mode(const struct pcie_device_state* dev,
                           pcie_irq_mode_info_t* out_info) {
    DEBUG_ASSERT(dev);
    if (!out_info)
        return ERR_INVALID_ARGS;

    status_t ret;

    MUTEX_ACQUIRE(((pcie_device_state_t*)dev), dev_lock);
    ret = dev->plugged_in
        ? pcie_get_irq_mode_internal(dev, out_info)
        : ERR_BAD_STATE;
    MUTEX_RELEASE(((pcie_device_state_t*)dev), dev_lock);

    return ret;
}

status_t pcie_set_irq_mode(pcie_device_state_t*    dev,
                           pcie_irq_mode_t         mode,
                           uint                    requested_irqs) {
    DEBUG_ASSERT(dev);
    status_t ret;

    MUTEX_ACQUIRE(dev, dev_lock);
    ret = dev->plugged_in
        ? pcie_set_irq_mode_internal(dev, mode, requested_irqs)
        : ERR_BAD_STATE;
    MUTEX_RELEASE(dev, dev_lock);

    return ret;
}

status_t pcie_register_irq_handler(pcie_device_state_t*  dev,
                                   uint                  irq_id,
                                   pcie_irq_handler_fn_t handler,
                                   void*                 ctx) {
    DEBUG_ASSERT(dev);
    status_t ret;

    MUTEX_ACQUIRE(dev, dev_lock);
    ret = dev->plugged_in
        ? pcie_register_irq_handler_internal(dev, irq_id, handler, ctx)
        : ERR_BAD_STATE;
    MUTEX_RELEASE(dev, dev_lock);

    return ret;
}

status_t pcie_mask_unmask_irq(pcie_device_state_t* dev,
                              uint                 irq_id,
                              bool                 mask) {
    DEBUG_ASSERT(dev);
    status_t ret;

    MUTEX_ACQUIRE(dev, dev_lock);
    ret = dev->plugged_in
        ? pcie_mask_unmask_irq_internal(dev, irq_id, mask)
        : ERR_BAD_STATE;
    MUTEX_RELEASE(dev, dev_lock);

    return ret;
}

/******************************************************************************
 *
 * Internal API; prototypes in pcie_priv.h
 *
 ******************************************************************************/
status_t pcie_init_device_irq_state(pcie_device_state_t* dev, pcie_bridge_state_t* upstream) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(!dev->irq.legacy.pin);
    DEBUG_ASSERT(!dev->irq.legacy.shared_handler);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));

    dev->irq.legacy.pin = pcie_read8(&dev->cfg->base.interrupt_pin);
    if (dev->irq.legacy.pin) {
        uint irq_id;

        irq_id = pcie_map_pin_to_irq(dev, dev->irq.legacy.pin, upstream);
        dev->irq.legacy.shared_handler = pcie_find_legacy_irq_handler(dev->bus_drv, irq_id);

        if (!dev->irq.legacy.shared_handler) {
            TRACEF("Failed to find or create shared legacy IRQ handler for "
                   "dev %02x:%02x.%01x (pin %u, irq id %u)\n",
                   dev->bus_id, dev->dev_id, dev->func_id,
                   dev->irq.legacy.pin, irq_id);
            return ERR_NO_RESOURCES;
        }
    }

    return NO_ERROR;
}

status_t pcie_init_irqs(pcie_bus_driver_state_t* bus_drv, const pcie_init_info_t* init_info) {
    DEBUG_ASSERT(bus_drv);
    DEBUG_ASSERT(init_info);

    if (bus_drv->legacy_irq_swizzle) {
        TRACEF("Failed to initialize PCIe IRQs; IRQs already initialized\n");
        return ERR_BAD_STATE;
    }

    if (!init_info->legacy_irq_swizzle) {
        TRACEF("No platform specific legacy IRQ swizzle supplied!\n");
        return ERR_INVALID_ARGS;
    }

    if (((init_info->alloc_msi_block == NULL) != (init_info->free_msi_block == NULL)) ||
        ((init_info->alloc_msi_block == NULL) != (init_info->register_msi_handler == NULL))) {
        TRACEF("Must provide all of the alloc/free/register msi callbacks, or none of them.  "
               "(alloc == %p, free == %p, register == %p)\n",
               init_info->alloc_msi_block,
               init_info->free_msi_block,
               init_info->register_msi_handler);
        return ERR_INVALID_ARGS;
    }

    bus_drv->legacy_irq_swizzle   = init_info->legacy_irq_swizzle;
    bus_drv->alloc_msi_block      = init_info->alloc_msi_block;
    bus_drv->free_msi_block       = init_info->free_msi_block;
    bus_drv->register_msi_handler = init_info->register_msi_handler;
    bus_drv->mask_unmask_msi      = init_info->mask_unmask_msi;

    return NO_ERROR;
}

void pcie_shutdown_irqs(pcie_bus_driver_state_t* bus_drv) {
    DEBUG_ASSERT(bus_drv);

    /* Shut off all of our IRQs and free all of our bookkeeping */
    pcie_legacy_irq_handler_state_t* handler;
    MUTEX_ACQUIRE(bus_drv, legacy_irq_list_lock);
    while ((handler = list_remove_head_type(&bus_drv->legacy_irq_list,
                                            pcie_legacy_irq_handler_state_t,
                                            legacy_irq_list_node)) != NULL) {
        DEBUG_ASSERT(list_is_empty(&handler->device_handler_list));
        mask_interrupt(handler->irq_id);
        register_int_handler(handler->irq_id, NULL, NULL);
        free(handler);
    }
    MUTEX_RELEASE(bus_drv, legacy_irq_list_lock);
}
