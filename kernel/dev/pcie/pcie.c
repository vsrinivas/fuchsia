// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <dev/pcie.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include "pcie_priv.h"

#define LOCAL_TRACE 0

/* TODO(johngro) : figure this out someday.
 *
 * In theory, BARs which map PIO regions for devices are supposed to be able to
 * use bits [2, 31] to describe the programable section of the PIO window.  On
 * real x86/64 systems, however, using the write-1s-readback technique to
 * determine programable bits of the BAR's address (and therefor the size of the
 * I/O window) shows that the upper 16 bits are not programable.  This makes
 * sense for x86 (where I/O space is only 16-bits), but fools the system into
 * thinking that the I/O window is enormous.
 *
 * For now, just define a mask which can be used during PIO window space
 * calculations which limits the size to 16 bits for x86/64 systems.  non-x86
 * systems are still free to use all of the bits for their PIO addresses
 * (although, it is still a bit unclear what it would mean to generate an IO
 * space cycle on an architecture which has no such thing as IO space).
 */
#if (defined(ARCH_X86) && ARCH_X86)
#define PCIE_HAS_IO_ADDR_SPACE (1)
#else
#define PCIE_HAS_IO_ADDR_SPACE (0)
#endif

#if PCIE_HAS_IO_ADDR_SPACE
#define PCIE_PIO_ADDR_SPACE_MASK (0xFFFF)
#define PCIE_PIO_ADDR_SPACE_SIZE (0x10000ull)
#else
#define PCIE_PIO_ADDR_SPACE_MASK (0xFFFFFFFF)
#define PCIE_PIO_ADDR_SPACE_SIZE (0x100000000ull)
#endif

#ifdef WITH_LIB_CONSOLE
#define EXPORT_TO_DEBUG_CONSOLE
#else
#define EXPORT_TO_DEBUG_CONSOLE static
#endif

static pcie_bus_driver_state_t g_drv_state; // The driver state singleton

#ifdef WITH_LIB_CONSOLE
pcie_bus_driver_state_t* pcie_get_bus_driver_state(void) { return &g_drv_state; }
#endif

// External references to the device driver registration tables.
extern pcie_driver_registration_t __start_pcie_builtin_drivers[] __WEAK;
extern pcie_driver_registration_t __stop_pcie_builtin_drivers[] __WEAK;

// Fwd decls
static void pcie_scan_bus(pcie_bridge_state_t* bridge);

EXPORT_TO_DEBUG_CONSOLE
pcie_config_t* pcie_get_config(const pcie_bus_driver_state_t* bus_drv,
                               uint64_t* cfg_phys,
                               uint bus_id,
                               uint dev_id,
                               uint func_id) {
    DEBUG_ASSERT(bus_drv);
    DEBUG_ASSERT(bus_id  < PCIE_MAX_BUSSES);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    for (size_t i = 0; i < bus_drv->ecam_window_count; ++i) {
        pcie_kmap_ecam_range_t* window = bus_drv->ecam_windows + i;
        pcie_ecam_range_t*      ecam   = &window->ecam;

        if ((bus_id >= ecam->bus_start) && (bus_id <= ecam->bus_end)) {
            size_t offset;

            bus_id -= ecam->bus_start;
            offset = (((size_t)bus_id)  << 20) |
                     (((size_t)dev_id)  << 15) |
                     (((size_t)func_id) << 12);

            DEBUG_ASSERT(window->vaddr);
            DEBUG_ASSERT(ecam->io_range.size >= PCIE_EXTENDED_CONFIG_SIZE);
            DEBUG_ASSERT(offset <= (ecam->io_range.size - PCIE_EXTENDED_CONFIG_SIZE));

            if (cfg_phys)
                *cfg_phys = window->ecam.io_range.bus_addr + offset;

            return (pcie_config_t*)(window->vaddr + offset);
        }
    }

    if (cfg_phys)
        *cfg_phys = 0;

    return NULL;
}

static status_t pcie_probe_bar_info(pcie_config_t*   cfg,
                                    uint             bar_id,
                                    uint             bar_count,
                                    pcie_bar_info_t* info_out) {
    DEBUG_ASSERT(cfg);
    DEBUG_ASSERT(bar_id < bar_count);
    DEBUG_ASSERT(info_out);

    memset(info_out, 0, sizeof(*info_out));

    /* Determine the type of BAR this is.  Make sure that it is one of the types we understand */
    volatile uint32_t* bar_reg = &cfg->base.base_addresses[bar_id];
    uint32_t bar_val           = pcie_read32(bar_reg);
    info_out->is_mmio          = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
    info_out->is_64bit         = info_out->is_mmio &&
                                 ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
    info_out->is_prefetchable  = info_out->is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);
    info_out->first_bar_reg    = bar_id;

    if (info_out->is_64bit) {
        if ((bar_id + 1) >= bar_count) {
            TRACEF("Illegal 64-bit MMIO BAR position (%u/%u) while fetching BAR info "
                   "for device config @%p\n",
                   bar_id, bar_count, cfg);
            return ERR_BAD_STATE;
        }
    } else {
        if (info_out->is_mmio && ((bar_val & PCI_BAR_MMIO_TYPE_MASK) != PCI_BAR_MMIO_TYPE_32BIT)) {
            TRACEF("Unrecognized MMIO BAR type (BAR[%u] == 0x%08x) while fetching BAR info "
                   "for device config @%p\n",
                   bar_id, bar_val, cfg);
            return ERR_BAD_STATE;
        }
    }

    /* Figure out the size of this BAR region by writing 1's to the
     * address bits, then reading back to see which bits the device
     * considers un-configurable. */
    uint32_t addr_mask = info_out->is_mmio ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;
    uint64_t size_mask;

    pcie_write32(bar_reg, bar_val | addr_mask);
    size_mask = ~(pcie_read32(bar_reg) & addr_mask);
    pcie_write32(bar_reg, bar_val);

    if (info_out->is_mmio) {
        if (info_out->is_64bit) {
            /* 64bit MMIO? Probe the upper bits as well */
            bar_reg++;
            bar_val = pcie_read32(bar_reg);
            pcie_write32(bar_reg, 0xFFFFFFFF);
            size_mask |= ((uint64_t)~pcie_read32(bar_reg)) << 32;
            pcie_write32(bar_reg, bar_val);
            info_out->size = size_mask + 1;
        } else {
            info_out->size = (uint32_t)(size_mask + 1);
        }
    } else {
        /* PIO BAR */
        info_out->size = ((uint32_t)(size_mask + 1)) & PCIE_PIO_ADDR_SPACE_MASK;
    }

    /* Success */
    return NO_ERROR;
}

static status_t pcie_enumerate_bars(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev && dev->cfg);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));

    pcie_config_t* cfg  = dev->cfg;
    uint8_t header_type = pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MASK;
    uint bar_count;

    static_assert(PCIE_MAX_BAR_REGS >= PCIE_BAR_REGS_PER_DEVICE, "");
    static_assert(PCIE_MAX_BAR_REGS >= PCIE_BAR_REGS_PER_BRIDGE, "");

    switch (header_type) {
    case PCI_HEADER_TYPE_STANDARD:
        bar_count = PCIE_BAR_REGS_PER_DEVICE;
        break;

    case PCI_HEADER_TYPE_PCI_BRIDGE:
        bar_count = PCIE_BAR_REGS_PER_BRIDGE;
        break;

    case PCI_HEADER_TYPE_CARD_BUS:
        goto finished;  // Nothing to do if this header type has no BARs

    default:
        TRACEF("Unrecognized header type (0x%02x) for device %02x:%02x:%01x.\n",
               header_type, dev->bus_id, dev->dev_id, dev->func_id);
        return ERR_NOT_SUPPORTED;
    }

    DEBUG_ASSERT(bar_count <= countof(dev->bars));
    for (uint i = 0; i < bar_count; ++i) {
        /* If this is a re-scan of the bus, We should not be re-enumerating BARs. */
        DEBUG_ASSERT(dev->bars[i].size == 0);
        DEBUG_ASSERT(!dev->bars[i].is_allocated);

        status_t probe_res = pcie_probe_bar_info(cfg, i, bar_count, &dev->bars[i]);
        if (probe_res != NO_ERROR)
            return probe_res;

        if (dev->bars[i].size > 0) {
            dev->bars[i].dev = dev;

            /* If this was a 64 bit bar, it took two registers to store.  Make
             * sure to skip the next register */
            if (dev->bars[i].is_64bit) {
                i++;

                if (i >= bar_count) {
                    TRACEF("Device %02x:%02x:%01x claims to have 64-bit BAR in position %u/%u!\n",
                           dev->bus_id, dev->dev_id, dev->func_id, i, bar_count);
                    return ERR_BAD_STATE;
                }
            }
        }
    }

finished:
    return NO_ERROR;
}

static status_t pcie_scan_init_device(pcie_device_state_t*     dev,
                                      pcie_bridge_state_t*     upstream_bridge,
                                      pcie_bus_driver_state_t* bus_drv,
                                      uint                     bus_id,
                                      uint                     dev_id,
                                      uint                     func_id) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->ref_count == 1);
    DEBUG_ASSERT(!dev->plugged_in);
    DEBUG_ASSERT(bus_drv);

    mutex_init(&dev->dev_lock);
    mutex_init(&dev->start_claim_lock);
    MUTEX_ACQUIRE(dev, dev_lock);

    status_t       status;
    uint64_t       cfg_phys;
    pcie_config_t* cfg = pcie_get_config(bus_drv, &cfg_phys, bus_id, dev_id, func_id);
    DEBUG_ASSERT(cfg);

    dev->cfg        = cfg;
    dev->cfg_phys   = cfg_phys;
    dev->bus_drv    = bus_drv;
    dev->vendor_id  = pcie_read16(&cfg->base.vendor_id);
    dev->device_id  = pcie_read16(&cfg->base.device_id);
    dev->class_id   = pcie_read8(&cfg->base.base_class);
    dev->subclass   = pcie_read8(&cfg->base.sub_class);
    dev->prog_if    = pcie_read8(&cfg->base.program_interface);
    dev->bus_id     = bus_id;
    dev->dev_id     = dev_id;
    dev->func_id    = func_id;

    /* PCI Express Capabilities */
    dev->pcie_caps.devtype = PCIE_DEVTYPE_UNKNOWN;

    /* Build this device's list of BARs with non-zero size, but do not actually
     * allocate them yet. */
    status = pcie_enumerate_bars(dev);
    if (status != NO_ERROR)
        goto finished;

    /* Parse and sanity check the capabilities and extended capabilities lists
     * if they exist */
    status = pcie_parse_capabilities(dev);
    if (status != NO_ERROR)
        goto finished;

    /* Now that we know what our capabilities are, initialize our internal IRQ
     * bookkeeping */
    status = pcie_init_device_irq_state(dev, upstream_bridge);

finished:
    MUTEX_RELEASE(dev, dev_lock);

    /* If things have gone well, and we have an upstream bridge, go ahead and
     * flag the device as plugged in, then link ourselves up to the upstream bridge. */
    if (status == NO_ERROR) {
        dev->plugged_in = true;
        if (upstream_bridge)
            pcie_link_device_to_upstream(dev, upstream_bridge);

        DEBUG_ASSERT((dev->upstream == NULL) == (upstream_bridge == NULL));
    } else {
        TRACEF("Failed to initialize device %02x:%02x:%01x; This is Very Bad.  "
               "Device (and any of its children) will be inaccessible!\n",
               bus_id, dev_id, func_id);
    }

    return status;
}

static void pcie_scan_function(pcie_bridge_state_t* upstream_bridge,
                               pcie_config_t*       cfg,
                               uint64_t             cfg_phys,
                               uint                 dev_id,
                               uint                 func_id) {
    DEBUG_ASSERT(upstream_bridge && cfg);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    pcie_device_state_t* dev = NULL;
    uint bus_id = upstream_bridge->managed_bus_id;
    __UNUSED uint ndx = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;

    DEBUG_ASSERT(ndx < countof(upstream_bridge->downstream));
    DEBUG_ASSERT(upstream_bridge->downstream[ndx] == NULL);

    /* Is there an actual device here? */
    uint16_t vendor_id = pcie_read16(&cfg->base.vendor_id);
    if (vendor_id == PCIE_INVALID_VENDOR_ID)
        return;

    LTRACEF("Scanning new function at %02x:%02x.%01x\n", bus_id, dev_id, func_id);

    /* If this function is a PCI bridge, extract the bus ID of the other side of
     * the bridge, initialize the bridge node and recurse.
     *
     * TODO(johngro) : Add some protection against cycles in the bridge
     * configuration which could lead to infinite recursion.
     */
    uint8_t header_type = pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MASK;
    if (header_type == PCI_HEADER_TYPE_PCI_BRIDGE) {
        pci_to_pci_bridge_config_t* bridge_cfg = (pci_to_pci_bridge_config_t*)(&cfg->base);

        uint primary_id   = pcie_read8(&bridge_cfg->primary_bus_id);
        uint secondary_id = pcie_read8(&bridge_cfg->secondary_bus_id);

        if (primary_id != bus_id) {
            TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x has invalid primary bus id "
                   "(%02x)... skipping scan.\n",
                   bus_id, dev_id, func_id, primary_id);
            return;
        }

        if (primary_id == secondary_id) {
            TRACEF("PCI-to-PCI bridge detected at %02x:%02x.%01x claims to be bridged to itsef "
                   "(primary %02x == secondary %02x)... skipping scan.\n",
                   bus_id, dev_id, func_id, primary_id, secondary_id);
            return;
        }

        /* Allocate and initialize our bridge structure */
        pcie_bridge_state_t* bridge = calloc(1, sizeof(*bridge));
        if (!bridge) {
            TRACEF("Failed to allocate bridge node for %02x:%02x.%01x during bus scan.\n",
                    bus_id, dev_id, func_id);
            return;
        }

        // TODO(johngro) : The local "dev" pointer owns this reference.  Clean
        // this up using smart pointers when we switch to C++
        dev = &bridge->dev;
        dev->ref_count = 1;
        dev->is_bridge = true;
        bridge->managed_bus_id = secondary_id;
    } else {
        /* Allocate and initialize our device structure */

        // TODO(johngro) : The local "dev" pointer owns this reference.  Clean
        // this up using smart pointers when we switch to C++
        dev = calloc(1, sizeof(*dev));
        if (!dev) {
            TRACEF("Failed to allocate device node for %02x:%02x.%01x during bus scan.\n",
                    bus_id, dev_id, func_id);
            return;
        }
        dev->ref_count = 1;
    }

    /* Initialize common fields, linking up the graph in the process. */
    status_t res = pcie_scan_init_device(dev,
                                         upstream_bridge,
                                         upstream_bridge->dev.bus_drv,
                                         bus_id, dev_id, func_id);
    if (NO_ERROR == res) {
        /* If this was a bridge device, recurse and continue probing. */
        pcie_bridge_state_t* bridge = pcie_downcast_to_bridge(dev);
        if (bridge)
            pcie_scan_bus(bridge);
    } else {
        /* Something went terribly wrong during init.  ASSERT that we are not
         * tracking this device upstream, and release it.  No need to log,
         * pcie_scan_init_device has done so already for us.
         */
        DEBUG_ASSERT(upstream_bridge->downstream[ndx] == NULL);
        pcie_release_device(dev);
        dev = NULL;
    }
}

static void pcie_scan_bus(pcie_bridge_state_t* bridge) {
    DEBUG_ASSERT(bridge);

    for (uint dev_id = 0; dev_id < PCIE_MAX_DEVICES_PER_BUS; ++dev_id) {
        for (uint func_id = 0; func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE; ++func_id) {
            /* Special case for the host controller. */
            if (!bridge->managed_bus_id && !dev_id && !func_id)
                continue;

            /* If we can find the config, and it has a valid vendor ID, go ahead
             * and scan it looking for a valid function. */
            uint64_t cfg_phys;
            pcie_config_t* cfg = pcie_get_config(bridge->dev.bus_drv, &cfg_phys,
                                                 bridge->managed_bus_id, dev_id, func_id);
            bool good_device = cfg && (pcie_read16(&cfg->base.vendor_id) != PCIE_INVALID_VENDOR_ID);
            if (good_device) {
                /* Don't scan the function again if we have already discovered
                 * it.  If this function happens to be a bridge, go ahead and
                 * look under it for new devices. */
                uint ndx    = (dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + func_id;
                DEBUG_ASSERT(ndx < countof(bridge->downstream));

                pcie_device_state_t* downstream = bridge->downstream[ndx];
                if (!downstream) {
                    pcie_scan_function(bridge, cfg, cfg_phys, dev_id, func_id);
                } else {
                    pcie_bridge_state_t* downstream_bridge = pcie_downcast_to_bridge(downstream);
                    if (downstream_bridge)
                        pcie_scan_bus(downstream_bridge);
                }
            }

            /* If this was function zero, and there is either no device, or the
             * config's header type indicates that this is not a multi-function
             * device, then just move on to the next device. */
            if (!func_id &&
               (!good_device || !(pcie_read8(&cfg->base.header_type) & PCI_HEADER_TYPE_MULTI_FN)))
                break;
        }
    }
}

void pcie_unplug_device(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);

    /* Begin by completely nerfing this device, and preventing an new API
     * operations on it.  We need to be inside the dev lock to do this.  Note:
     * it is assumed that we will not disappear during any of this function,
     * because our caller is holding a reference to us.  This will be much
     * easier to prove when we switch to C++ and start using utils:RefCounted
     * pointers instead of raw C pointers. */
    MUTEX_ACQUIRE(dev, dev_lock);

    /* ASSERT that any driver which may have been associated with this
     * device has been properly shutdown already. */
    DEBUG_ASSERT(!dev->driver);
    DEBUG_ASSERT(!dev->driver_ctx);
    DEBUG_ASSERT(!dev->started);

    if (dev->plugged_in) {
        /* Remove all access this device has to the PCI bus */
        pcie_write16(&dev->cfg->base.command, PCIE_CFG_COMMAND_INT_DISABLE);

        /* TODO(johngro) : Make sure that our interrupt mode has been set to
         * completely disabled.  Do not return allocated BARs to the central
         * pool yet.  These regions of the physical bus need to remain
         * "allocated" until all drivers/users in the system release their last
         * reference to the device.  This way, if the device gets plugged in
         * again immediately, the new version of the device will not end up
         * getting mapped underneath any stale driver instances. */

        dev->plugged_in = false;
    } else {
        /* TODO(johngro) : Assert that the device has been completely disabled. */
    }

    MUTEX_RELEASE(dev, dev_lock);

    /* If this is a bridge, recursively unplug its children */
    pcie_bridge_state_t* bridge = pcie_downcast_to_bridge(dev);
    if (bridge) {
        for (size_t i = 0; i < countof(bridge->downstream); ++i) {
            pcie_device_state_t* downstream_device = pcie_get_refed_downstream(bridge, i);

            if (downstream_device) {
                pcie_unplug_device(downstream_device);
                pcie_release_device(downstream_device);
            }
        }
    }

    /* Unlink ourselves from our upstream parent (if we still have one). */
    pcie_unlink_device_from_upstream(dev);
}

static bool pcie_allocate_bar(pcie_bar_info_t* info) {
    DEBUG_ASSERT(info);
    DEBUG_ASSERT(info->dev);
    DEBUG_ASSERT(info->dev->bus_drv);
    DEBUG_ASSERT(info->dev->cfg);

    /* Do not attempt to remap if we are rescanning the bus and this BAR is
     * already allocated */
    if (info->is_allocated)
        return true;

    pcie_device_state_t*     dev = info->dev;
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    pcie_config_t*           cfg = dev->cfg;

    /* Choose which range we will attempt to allocate from, then check to see if we have the
     * space. */
    pcie_io_range_alloc_t* io_range = !info->is_mmio
                                    ? &bus_drv->pio
                                    : (info->is_64bit ? &bus_drv->mmio_hi : &bus_drv->mmio_lo);
    uint32_t addr_mask = info->is_mmio
                       ? PCI_BAR_MMIO_ADDR_MASK
                       : PCI_BAR_PIO_ADDR_MASK;

    /* If check to see if we have the space to allocate within the chosen
     * range.  In the case of a 64 bit MMIO BAR, if we run out of space in
     * the high-memory MMIO range, try the low memory range as well.
     */
    uint64_t align_size, avail, alloc_start, align_overhead, real_alloc_start;
    while (true) {
        /* MMIO windows and I/O windows on systems where I/O space is actually
         * memory mapped must be aligned to a page boundary, at least. */
        bool is_io_space = PCIE_HAS_IO_ADDR_SPACE && !info->is_mmio;
        DEBUG_ASSERT(io_range->used <= io_range->io.size);
        align_size     = ((info->size >= PAGE_SIZE) || is_io_space)
                       ? info->size
                       : PAGE_SIZE;
        avail          = (io_range->io.size - io_range->used);

        alloc_start    = io_range->io.bus_addr + io_range->used;
        real_alloc_start = ROUNDUP(alloc_start, align_size);
        align_overhead = real_alloc_start - alloc_start;

        if ((avail < align_overhead) ||
           ((avail - align_overhead) < info->size)) {

            if (io_range == &bus_drv->mmio_hi) {
                LTRACEF("Insufficient space to map 64-bit MMIO BAR in high region while "
                        "configuring BARs for device at %02x:%02x.%01x (cfg vaddr = %p).  Falling "
                        "back on low memory region.\n",
                        dev->bus_id, dev->dev_id, dev->func_id, cfg);
                io_range = &bus_drv->mmio_lo;
                continue;
            }

            TRACEF("Insufficient space to map BAR region while configuring BARs for device at "
                   "%02x:%02x.%01x (cfg vaddr = %p)\n",
                   dev->bus_id, dev->dev_id, dev->func_id, cfg);
            TRACEF("BAR region size %#" PRIx64 " Alignment overhead %#" PRIx64
                   " Space available %#" PRIx64 "\n",
                   info->size, align_overhead, avail);

            return false;
        }

        break;
    }

    /* Looks like we have enough space.  Update our range bookkeeping,
     * and record our allocated and aligned physical address in our
     * BAR(s) */
    volatile uint32_t* bar_reg = &cfg->base.base_addresses[info->first_bar_reg];

    io_range->used += (size_t)(align_overhead + align_size);
    DEBUG_ASSERT(!(real_alloc_start & ~addr_mask));

    pcie_write32(bar_reg, (uint32_t)(real_alloc_start & 0xFFFFFFFF) | (pcie_read32(bar_reg) & ~addr_mask));
    if (info->is_64bit)
        pcie_write32(bar_reg + 1, (uint32_t)(real_alloc_start >> 32));

    /* Success!  Fill out the info structure and we are done. */
    info->bus_addr     = real_alloc_start;
    info->is_allocated = true;
    return true;
}

static status_t pcie_allocate_bars(pcie_device_state_t* dev) {
    /* TODO(johngro) : This method should be much smarter.  Right now, it just
     * allocates the BARs in the order it happens to enumerate them in, paying
     * no attention to the bridge topology, nor making any effort to be
     * efficient in how it divides up the available regions.
     *
     * Moving forward, it needs to do a better job.  It should allocate in a
     * depth first fashion across the bridge tree (to make certain that bridge
     * regions do not overlap), and within each bridge region, apply some
     * heuristic to achieve efficient alignment and packing (probably allocating
     * the largest regions first is a good start)
     */
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(dev->cfg);
    MUTEX_ACQUIRE(dev, dev_lock);

    status_t ret;

    /* Has the device been unplugged already? */
    if (!dev->plugged_in) {
        ret = ERR_UNAVAILABLE;
        goto finished;
    }

    /* If this has been claimed by a driver, do not make any changes
     * to the BAR allocation. */
    if (dev->driver) {
        ret = NO_ERROR;
        goto finished;
    }

    /* Make sure the device has no access to either MMIO or PIO space, cannot
     * access main system memory, and has its IRQ(s) disabled */
    pcie_write16(&dev->cfg->base.command, PCIE_CFG_COMMAND_INT_DISABLE);

    /* Allocate BARs for the device */
    for (size_t i = 0; i < countof(dev->bars); ++i) {
        if (dev->bars[i].size)
            pcie_allocate_bar(&dev->bars[i]);
    }

    /* If this is a bridge, recurse and keep allocating */
    pcie_bridge_state_t* bridge = pcie_downcast_to_bridge(dev);
    if (bridge) {
        for (size_t i = 0; i < countof(bridge->downstream); ++i) {
            if (bridge->downstream[i]) {
                pcie_allocate_bars(bridge->downstream[i]);
            }
        }
    }

    ret = NO_ERROR;

finished:
    MUTEX_RELEASE(dev, dev_lock);
    return ret;
}

static bool pcie_allocate_device_bars_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev);
    pcie_allocate_bars(dev);
    return true;
}

static void pcie_allocate_device_bars(pcie_bus_driver_state_t* bus_drv) {
    pcie_foreach_device(bus_drv, pcie_allocate_device_bars_helper, NULL);
}

static status_t pcie_claim_device(pcie_device_state_t* dev,
                                  const pcie_driver_registration_t* driver,
                                  void* driver_ctx) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(driver && driver->fn_table);
    DEBUG_ASSERT(is_mutex_held(&dev->start_claim_lock));

    if (dev->driver) {
        return ERR_ALREADY_BOUND;
    }

    status_t ret;
    MUTEX_ACQUIRE(dev, dev_lock);

    /* Has the device been unplugged? */
    if (!dev->plugged_in) {
        ret = ERR_UNAVAILABLE;
        goto finished;
    }

    /* Looks good!  Claim the device in the name of the driver. */
    dev->driver     = driver;
    dev->driver_ctx = driver_ctx;
    ret             = NO_ERROR;

finished:
    MUTEX_RELEASE(dev, dev_lock);
    return ret;
}

static bool pcie_claim_devices_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev);

    /* Our device is currently ref'ed, so we know it will not disappear out from
     * under us.  While holding the device's start/claim lock, iterate over our
     * list of built-in kernel drivers and see if any of them want to claim this
     * device.  Do not hold the device's API lock while calling the driver's
     * probe method.  The driver is going to interact with the device using the
     * public facing API, which will need to obtain the API lock for pretty much
     * every operation.
     */
    MUTEX_ACQUIRE(dev, start_claim_lock);

    /* If we have already been claimed, just move on to the next device */
    if (dev->driver)
        goto finished;

    /* Go over our list of builtin drivers and see if any are interested in this
     * device. */
    void* driver_ctx = NULL;
    const pcie_driver_registration_t* driver;

    for (driver = __start_pcie_builtin_drivers; driver < __stop_pcie_builtin_drivers; ++driver) {
        const pcie_driver_fn_table_t* fn_table = driver->fn_table;
        DEBUG_ASSERT(fn_table->pcie_probe_fn);
        driver_ctx = fn_table->pcie_probe_fn(dev);

        if (driver_ctx)
            break;
    }

    /* If no one wanted the device, just move on to the next one. */
    if (!driver_ctx)
        goto finished;

    /* Looks like we found a driver who is interested in the device.  Attempt to
     * claim it in the name of the driver.  This might fail because the device
     * may have become unplugged.  If so, give the device context back to the
     * driver using its release method (if it has one).
     */
    status_t res = pcie_claim_device(dev, driver, driver_ctx);
    if (res != NO_ERROR) {
        if (driver->fn_table->pcie_release_fn)
            driver->fn_table->pcie_release_fn(driver_ctx);
    }

    /* Move on to the next device */
finished:
    MUTEX_RELEASE(dev, start_claim_lock);
    return true;
}

static void pcie_claim_devices(pcie_bus_driver_state_t* bus_drv) {
    pcie_foreach_device(bus_drv, pcie_claim_devices_helper, NULL);
}

static status_t pcie_start_device(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(is_mutex_held(&dev->start_claim_lock));

    /* Note: We do not obtain the dev_lock during this method.  We are going to
     * be calling back through the start hook of the driver, and we cannot be
     * holding the device lock when we do this (since the driver needs to
     * interact with the PCIe API in order to start).  If the device is
     * spontaneously unplugged during this startup process, the driver should
     * fail at some point while interacting with the API and fail the startup
     * process.
     *
     * TODO(johngro): At some point, when we have the infrastructure in place,
     * the unplug operation needs to notify the device driver that it has become
     * spontaneously unplugged.  For sanity's sake, this operation will need to
     * be synchronized with the start/claim process.
     */

    /* Was the device was already un-claimed */
    if (!dev->driver)
        return ERR_BAD_STATE;

    /* Was the device was already started?  If so, great.  Just declare success */
    if (dev->started)
        return NO_ERROR;

    /* Attempt to start the device */
    status_t ret = NO_ERROR;
    const pcie_driver_fn_table_t* fn_table = dev->driver->fn_table;
    if (fn_table->pcie_startup_fn && ((ret = fn_table->pcie_startup_fn(dev)) != NO_ERROR)) {
        /* Device failed to start.*/
        TRACEF("Failed to start %04hx:%04hx at %02x:%02x.%01x claimed by driver "
               "\"%s\" (result %d)\n",
               pcie_read16(&dev->cfg->base.vendor_id),
               pcie_read16(&dev->cfg->base.device_id),
               dev->bus_id,
               dev->dev_id,
               dev->func_id,
               pcie_driver_name(dev->driver),
               ret);

        /* Call the driver release method (if any) */
        if (fn_table->pcie_release_fn)
            fn_table->pcie_release_fn(dev->driver_ctx);

        /* Clear out any internal driver related bookkeeping */
        dev->driver     = NULL;
        dev->driver_ctx = NULL;
        DEBUG_ASSERT(!dev->started);
    } else {
        dev->started = true;
    }

    return ret;
}

static bool pcie_start_devices_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev);

    /* Don't let the started/claimed status of the device change for the
     * duration of this operaion */
    MUTEX_ACQUIRE(dev, start_claim_lock);
    pcie_start_device(dev);
    MUTEX_RELEASE(dev, start_claim_lock);

    return true;
}

static void pcie_start_devices(pcie_bus_driver_state_t* bus_drv) {
    pcie_foreach_device(bus_drv, pcie_start_devices_helper, NULL);
}

typedef struct pcie_get_nth_device_state {
    uint32_t index;
    pcie_device_state_t* ret;
} pcie_get_nth_device_state_t;

static bool pcie_get_nth_device_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev && ctx);

    pcie_get_nth_device_state_t* state = (pcie_get_nth_device_state_t*)ctx;
    if (!state->index) {
        state->ret = dev;
        pcie_add_ref_device(state->ret);
        return false;
    }

    state->index--;
    return true;
}

/*
 * For iterating through all PCI devices. Returns the nth device, or NULL
 * if index is >= the number of PCI devices.
 */
pcie_device_state_t* pcie_get_nth_device(uint32_t index) {
    pcie_bus_driver_state_t* bus_drv = pcie_get_bus_driver_state();
    if (!bus_drv) return NULL;

    pcie_get_nth_device_state_t state = { .index = index, .ret = NULL };
    pcie_foreach_device(bus_drv, pcie_get_nth_device_helper, &state);

    return state.ret;
}

/*
 * Attaches a driver to a PCI device. Returns ERR_ALREADY_BOUND if the device has already been
 * claimed by another driver.
 */
status_t pcie_claim_and_start_device(pcie_device_state_t* dev,
                                     const pcie_driver_registration_t* driver,
                                     void* driver_ctx) {
    status_t result;

    /* Don't allow the claimed/started state to chage during this operation */
    MUTEX_ACQUIRE(dev, start_claim_lock);

    result = pcie_claim_device(dev, driver, driver_ctx);
    if (result != NO_ERROR)
        goto finished;

    /* No special actions need to be taken if we fail to start the device.  It
     * will automatically have been unclaimed for us. */
    result = pcie_start_device(dev);

finished:
    MUTEX_RELEASE(dev, start_claim_lock);
    return result;
}

/*
 * Shutdown and unclaim a device had been successfully claimed with
 * pcie_claim_and_start_device()
 */
void pcie_shutdown_device(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    MUTEX_ACQUIRE(dev, start_claim_lock);

    /* If the device is not claimed, then there is nothing to do */
    if (!dev->driver) {
        DEBUG_ASSERT(!dev->driver_ctx);
        DEBUG_ASSERT(!dev->started);
        LTRACEF("PCI device (%02x:%02x.%x) already shutdown.\n",
                dev->bus_id, dev->dev_id, dev->func_id);
        goto finished;
    }

    const pcie_driver_fn_table_t* fn = dev->driver->fn_table;

    LTRACEF("Shutting down PCI device %02x:%02x.%x (%s)...\n",
            dev->bus_id, dev->dev_id, dev->func_id,
            pcie_driver_name(dev->driver));

    /* If startup was ever successfully called for this device, and the device
     * has registered a shutdown hook, give it a chance to shutdown cleanly */
    if (dev->started && fn->pcie_shutdown_fn)
        fn->pcie_shutdown_fn(dev);
    dev->started = false;

    /* Make sure that all IRQs are shutdown and all handlers released for this device */
    pcie_set_irq_mode_disabled(dev);

    /* Disable access to MMIO windows, PIO windows, and system memory */
    pcie_write16(&dev->cfg->base.command, PCIE_CFG_COMMAND_INT_DISABLE);

    /* If the device has a release hook, call it in order to allow it to free
     * any dynamically allocated resources. */
    if (fn->pcie_release_fn)
        fn->pcie_release_fn(dev->driver_ctx);

    /* Unclaim the device. */
    dev->driver     = NULL;
    dev->driver_ctx = NULL;

finished:
    MUTEX_RELEASE(dev, start_claim_lock);
}

status_t pcie_do_function_level_reset(pcie_device_state_t* dev) {
    status_t ret;
    DEBUG_ASSERT(dev);

    // TODO(johngro) : Function level reset is an operation which can take quite
    // a long time (more than a second).  We should not hold the device lock for
    // the entire duration of the operation.  This should be re-done so that the
    // device can be placed into a "resetting" state (and other API calls can
    // fail with ERR_BAD_STATE, or some-such) and the lock can be released while the
    // reset timeouts run.  This way, a spontaneous unplug event can occur and
    // not block the whole world because the device unplugged was in the process
    // of a FLR.
    MUTEX_ACQUIRE(dev, dev_lock);

    // Make certain to check to see if the device is still plugged in.
    if (!dev->plugged_in) {
        ret = ERR_UNAVAILABLE;
        goto finished;
    }

    // Disallow reset if we currently have an active IRQ mode.
    //
    // Note: the only possible reason for get_irq_mode to fail would be for the
    // device to be unplugged.  Since we have already checked for that, we
    // assert that the call should succeed.
    pcie_irq_mode_info_t irq_mode_info;
    ret = pcie_get_irq_mode_internal(dev, &irq_mode_info);
    DEBUG_ASSERT(NO_ERROR == ret);
    if (irq_mode_info.mode != PCIE_IRQ_MODE_DISABLED) {
        ret = ERR_BAD_STATE;
        goto finished;
    }
    DEBUG_ASSERT(!irq_mode_info.registered_handlers);
    DEBUG_ASSERT(!irq_mode_info.max_handlers);

    // If cannot reset via the PCIe capability, or the PCI advanced capability,
    // then this device simply does not support function level reset.
    if (!dev->pcie_caps.has_flr && !dev->pcie_adv_caps.has_flr) {
        ret = ERR_NOT_SUPPORTED;
        goto finished;
    }

    if (dev->pcie_caps.has_flr) {
        // TODO: perform function level reset using PCIe Capability Structure.
        TRACEF("TODO(johngro): Implement PCIe Capability FLR\n");
        ret = ERR_NOT_SUPPORTED;
    }

    if (dev->pcie_adv_caps.has_flr) {
        // Following the procedure outlined in the Implementation notes
        spin_lock_saved_state_t  irq_state;
        pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
        pcie_config_t*           cfg = dev->cfg;
        uint32_t                 bar_backup[PCIE_MAX_BAR_REGS];
        uint16_t                 cmd_backup;
        uint                     bar_count = pcie_downcast_to_bridge(dev)
                                           ? PCIE_BAR_REGS_PER_BRIDGE
                                           : PCIE_BAR_REGS_PER_DEVICE;

        // 1) Make sure driver code is not creating new transactions (not much I
        //    can do about this, just have to hope).
        // 2) Clear out the command register so that no new transactions may be
        //    initiated.  Also back up the BARs in the process.
        spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);

        cmd_backup = pcie_read16(&cfg->base.command);
        pcie_write16(&cfg->base.command, PCIE_CFG_COMMAND_INT_DISABLE);
        for (uint i = 0; i < bar_count; ++i)
            bar_backup[i] = pcie_read32(&cfg->base.base_addresses[i]);

        spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);

        // 3) Poll the transaction pending bit until it clears.  This may take
        //    "several seconds"
        lk_time_t start = current_time();
        ret = ERR_TIMED_OUT;
        do {
            if (!(pcie_read8(&dev->pcie_adv_caps.ecam->af_status) &
                  PCS_ADVCAPS_STATUS_TRANS_PENDING)) {
                ret = NO_ERROR;
                break;
            }
            thread_sleep(1);
        } while ((current_time() - start) < 5000);

        if (ret != NO_ERROR) {
            TRACEF("Timeout waiting for pending transactions to clear the bus "
                   "for %02x:%02x.%01x\n",
                   dev->bus_id, dev->dev_id, dev->func_id);

            // Restore the command register
            spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);
            pcie_write16(&cfg->base.command, cmd_backup);
            spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);

            goto finished;
        } else {
            // 4) Software initiates the FLR
            pcie_write8(&dev->pcie_adv_caps.ecam->af_ctrl, PCS_ADVCAPS_CTRL_INITIATE_FLR);

            // 5) Software waits 100mSec
            thread_sleep(100);
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
            if (pcie_read16(&dev->cfg->base.vendor_id) != PCIE_INVALID_VENDOR_ID) {
                ret = NO_ERROR;
                break;
            }
            thread_sleep(1);
        } while ((current_time() - start) < 5000);

        if (ret == NO_ERROR) {
            // 6) Software reconfigures the function and enables it for normal operation
            spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);

            for (uint i = 0; i < bar_count; ++i)
                pcie_write32(&cfg->base.base_addresses[i], bar_backup[i]);
            pcie_write16(&cfg->base.command, cmd_backup);

            spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);
        } else {
            // TODO(johngro) : What do we do if this fails?  If we trigger a
            // device reset, and the device fails to re-appear after 5 seconds,
            // it is probably gone for good.  We probably need to force unload
            // any device drivers which had previously owned the device.
            TRACEF("Timeout waiting for %02x:%02x.%01x to complete function "
                   "level reset.  This is Very Bad.\n",
                   dev->bus_id, dev->dev_id, dev->func_id);
        }
    }

finished:
    MUTEX_RELEASE(dev, dev_lock);
    return ret;
}

EXPORT_TO_DEBUG_CONSOLE
void pcie_scan_and_start_devices(pcie_bus_driver_state_t* bus_drv) {
    DEBUG_ASSERT(bus_drv);

    MUTEX_ACQUIRE(bus_drv, bus_rescan_lock);

    /* If we have not already discovered the host bridge, start by looking for it */
    if (!bus_drv->host_bridge) {
        /* 00:00.0 had better exist and be a host bridge, or we are not going to proceed. */
        uint64_t cfg_phys;
        pcie_config_t* cfg = pcie_get_config(bus_drv, &cfg_phys, 0, 0, 0);
        if (!cfg) {
            TRACEF("Failed to find configuration for root host bridge (device 00:00.0)\n");
            return;
        }

        uint16_t vendor_id = pcie_read16(&cfg->base.vendor_id);
        if (vendor_id == PCIE_INVALID_VENDOR_ID) {
            TRACEF("No device detected at 00:00.0\n");
            return;
        }

        /* TODO(johngro) : make a header file with all of the classes, subclasses
         * and programming interface magic numbers. */
        uint8_t base_class = pcie_read8(&cfg->base.base_class);
        uint8_t sub_class  = pcie_read8(&cfg->base.sub_class);
        if ((base_class != 0x06) && (sub_class != 0x00)) {
            TRACEF("Device 00:00.0 is not a host bridge (base 0x%02x sub 0x%02x)\n",
                   base_class, sub_class);
            return;
        }

        /* Allocate the host bridge, initialize the structure and start the scan. */
        pcie_bridge_state_t* root;
        root = bus_drv->host_bridge = calloc(1, sizeof(*bus_drv->host_bridge));
        if (!root) {
            TRACEF("Failed to allocate bridge node for 00:00.0 during bus scan.\n");
            return;
        }
        bus_drv->host_bridge->dev.ref_count = 1;    // bus_drv->host_bridge owns this ref

        /* Initialize common fields */
        status_t res = pcie_scan_init_device(&bus_drv->host_bridge->dev, NULL, bus_drv, 0, 0, 0);
        if (NO_ERROR != res) {
            /* No need to log, init device has done so already for us. */
            pcie_release_device(&bus_drv->host_bridge->dev);
            bus_drv->host_bridge = NULL;
            goto finished;
        }

        root->dev.is_bridge = true;
    }

    /* Now that we have found the host bridge, scan the bus it controls, looking
     * for devices and other bridges. */
    pcie_scan_bus(bus_drv->host_bridge);

    /* Now that we have scanned the config space, allocate I/O widows for each
     * of the BARs discovered by carving up the regions of the system and I/O
     * buses which were provided to us by the platform. */
    pcie_allocate_bars(&bus_drv->host_bridge->dev);

    /* Go over our tree and look for drivers who might want to take ownership of
     * device. */
    pcie_claim_devices(bus_drv);

    /* Give the devices claimed by drivers a chance to start */
    pcie_start_devices(bus_drv);

finished:
    MUTEX_RELEASE(bus_drv, bus_rescan_lock);
}

static void pcie_early_init_hook(uint level) {
    pcie_bus_driver_state_t* bus_drv = &g_drv_state;

    mutex_init(&bus_drv->bus_topology_lock);
    mutex_init(&bus_drv->bus_rescan_lock);
    mutex_init(&bus_drv->legacy_irq_list_lock);

    spin_lock_init (&bus_drv->legacy_irq_handler_lock);
    list_initialize(&bus_drv->legacy_irq_list);
}
LK_INIT_HOOK(pcie_early_init, pcie_early_init_hook, LK_INIT_LEVEL_PLATFORM - 1);

status_t pcie_init(const pcie_init_info_t* init_info) {
    pcie_bus_driver_state_t* bus_drv = &g_drv_state;
    status_t status = NO_ERROR;

    if (!init_info) {
        TRACEF("Failed to initialize PCIe bus driver; no init info provided");
        return ERR_INVALID_ARGS;
    }

    if (bus_drv->ecam_windows) {
        TRACEF("Failed to initialize PCIe bus driver; driver already initialized\n");
        return ERR_BAD_STATE;
    }

    /* Initialize our state */
    status = pcie_init_irqs(bus_drv, init_info);
    if (status != NO_ERROR)
        return status;

    bus_drv->mmio_lo.io   = init_info->mmio_window_lo;
    bus_drv->mmio_hi.io   = init_info->mmio_window_hi;
    bus_drv->pio.io       = init_info->pio_window;
    bus_drv->mmio_lo.used = 0;
    bus_drv->mmio_hi.used = 0;
    bus_drv->pio.used     = 0;

    /* Sanity check the init info provided by the platform.  If anything goes
     * wrong in this section, the error will be INVALID_ARGS, so just set the
     * status to that for now to save typing. */
    status = ERR_INVALID_ARGS;

    /* Start by checking the ECAM window requirements */
    if (!init_info->ecam_windows) {
        TRACEF("Invalid ecam_windows pointer (%p)!\n", init_info->ecam_windows);
        goto bailout;
    }

    if (!init_info->ecam_window_count) {
        TRACEF("Invalid ecam_window_count (%zu)!\n", init_info->ecam_window_count);
        goto bailout;
    }

    if (!init_info->ecam_windows[0].bus_start == 0) {
        TRACEF("First ECAM window provided by platform does not include bus 0 (bus_start = %u)\n",
                init_info->ecam_windows[0].bus_start);
        goto bailout;
    }

    for (size_t i = 0; i < init_info->ecam_window_count; ++i) {
        const pcie_ecam_range_t* window = init_info->ecam_windows + i;
        if (window->bus_start > window->bus_end) {
            TRACEF("First ECAM window[%zu]'s bus_start exceeds bus_end (%u > %u)\n",
                    i, window->bus_start, window->bus_end);
            goto bailout;
        }

        if (window->io_range.size < ((window->bus_end - window->bus_start + 1u) *
                                     PCIE_ECAM_BYTE_PER_BUS)) {
            TRACEF("ECAM window[%zu]'s size is too small to manage %u buses (%zu < %zu)\n",
                    i,
                    (window->bus_end - window->bus_start + 1u),
                    (size_t)(window->io_range.size),
                    (size_t)((window->bus_end - window->bus_start + 1u) * PCIE_ECAM_BYTE_PER_BUS));
            goto bailout;
        }

        if (i) {
            const pcie_ecam_range_t* prev = init_info->ecam_windows + i - 1;

            if (window->bus_start <= prev->bus_end) {
                TRACEF("ECAM windows not in strictly monotonically increasing "
                       "bus region order.  (window[%zu].start <= window[%zu].end; %u <= %u)\n",
                        i, i - 1,
                        window->bus_start, prev->bus_end);
                goto bailout;
            }
        }
    }

    /* The MMIO low memory region must be below the physical 4GB mark */
    if (((bus_drv->mmio_lo.io.bus_addr + 0ull) >= 0x100000000ull) ||
        ((bus_drv->mmio_lo.io.size     + 0ull) >= 0x100000000ull) ||
         (bus_drv->mmio_lo.io.bus_addr         > (0x100000000ull - bus_drv->mmio_lo.io.size))) {
        TRACEF("Low mem MMIO region [%zx, %zx) does not exist entirely below 4GB mark.\n",
                (size_t)bus_drv->mmio_lo.io.bus_addr,
                (size_t)bus_drv->mmio_lo.io.bus_addr + bus_drv->mmio_lo.io.size);
        goto bailout;
    }

    /* The PIO region must fit somewhere in the architecture's I/O bus region */
    if (((bus_drv->pio.io.bus_addr + 0ull) >= PCIE_PIO_ADDR_SPACE_SIZE) ||
        ((bus_drv->pio.io.size     + 0ull) >= PCIE_PIO_ADDR_SPACE_SIZE) ||
         (bus_drv->pio.io.bus_addr         > (PCIE_PIO_ADDR_SPACE_SIZE - bus_drv->pio.io.size))) {
        TRACEF("PIO region [%zx, %zx) too large for architecture's PIO address space size (%zx)\n",
                (size_t)bus_drv->pio.io.bus_addr,
                (size_t)bus_drv->pio.io.bus_addr + bus_drv->pio.io.size,
                (size_t)PCIE_PIO_ADDR_SPACE_SIZE);
        goto bailout;
    }

    /* Init parameters look good, go back to assuming NO_ERROR for now. */
    status = NO_ERROR;

    /* Stash the ECAM window info and map the ECAM windows into the bus driver's
     * address space so we can access config space. */
    bus_drv->ecam_window_count = init_info->ecam_window_count;
    bus_drv->ecam_windows = (pcie_kmap_ecam_range_t*)calloc(bus_drv->ecam_window_count,
                                                        sizeof(*bus_drv->ecam_windows));
    if (!bus_drv->ecam_windows) {
        TRACEF("Failed to initialize PCIe bus driver; could not allocate %zu "
               "bytes for ECAM window state\n",
               bus_drv->ecam_window_count * sizeof(*bus_drv->ecam_windows));
        status = ERR_NO_MEMORY;
        goto bailout;
    }

    /* TODO(johngro) : Don't grab a reference to the kernel's address space.
     * What we want is a reference to our current address space (The one that
     * the bus driver will run in). */
    DEBUG_ASSERT(!bus_drv->aspace);
    bus_drv->aspace = vmm_get_kernel_aspace();
    if (bus_drv->aspace == NULL) {
        TRACEF("Failed to initialize PCIe bus driver; could not obtain handle "
               "to kernel address space\n");
        status = ERR_INTERNAL;
        goto bailout;
    }

    for (size_t i = 0; i < bus_drv->ecam_window_count; ++i) {
        pcie_kmap_ecam_range_t* window = &bus_drv->ecam_windows[i];
        pcie_ecam_range_t*      ecam   = &window->ecam;
        char name_buf[32];

        *ecam = init_info->ecam_windows[i];

        if ((ecam->io_range.size     & ((size_t)PAGE_SIZE   - 1)) ||
            (ecam->io_range.bus_addr & ((uint64_t)PAGE_SIZE - 1))) {
            TRACEF("Failed to initialize PCIe bus driver; Invalid ECAM window "
                   "%#zx @ %#" PRIx64 ").  Windows must be page aligned and a "
                   "multiple of pages in length.\n",
                   ecam->io_range.size, ecam->io_range.bus_addr);
            status = ERR_INVALID_ARGS;
            goto bailout;
        }

        snprintf(name_buf, sizeof(name_buf), "pcie_cfg%zu", i);
        name_buf[sizeof(name_buf) - 1] = 0;

        status = vmm_alloc_physical(
                bus_drv->aspace,
                name_buf,
                ecam->io_range.size,
                &window->vaddr,
                PAGE_SIZE_SHIFT,
                0 /* min alloc gap */,
                ecam->io_range.bus_addr,
                0 /* vmm flags */,
                ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                    ARCH_MMU_FLAG_PERM_WRITE);
        if (status != NO_ERROR) {
            TRACEF("Failed to initialize PCIe bus driver; Failed to map ECAM window "
                   "(%#zx @ %#" PRIx64 ").  Status = %d.\n",
                   ecam->io_range.size, ecam->io_range.bus_addr, status);
            goto bailout;
        }
    }

    /* Scan the bus and start up any drivers who claim devices we discover */
    pcie_scan_and_start_devices(bus_drv);

bailout:
    if (status != NO_ERROR)
        pcie_shutdown();

    return status;
}

static bool pcie_shutdown_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev);
    pcie_shutdown_device(dev);
    return true;
}

void pcie_shutdown(void) {
    pcie_bus_driver_state_t* bus_drv = &g_drv_state;

    /* Force shutdown any devices which happen to still be running. */
    pcie_foreach_device(bus_drv, pcie_shutdown_helper, NULL);

    /* Shut off all of our IRQs and free all of our bookkeeping */
    pcie_shutdown_irqs(bus_drv);

    /* Free the device tree */
    if (bus_drv->host_bridge) {
        pcie_unplug_device(&bus_drv->host_bridge->dev);
        bus_drv->host_bridge = NULL;
    }

    /* Free the ECAM window bookkeeping */
    if (bus_drv->ecam_windows) {
        DEBUG_ASSERT(bus_drv->aspace);

        for (size_t i = 0; i < bus_drv->ecam_window_count; ++i) {
            pcie_kmap_ecam_range_t* window = &bus_drv->ecam_windows[i];

            if (window->vaddr)
                vmm_free_region(bus_drv->aspace, (vaddr_t)window->vaddr);
        }

        free(bus_drv->ecam_windows);
    }

    memset(bus_drv, 0, sizeof(*bus_drv));
}

void pcie_modify_cmd_internal(pcie_device_state_t* dev, uint16_t clr_bits, uint16_t set_bits) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(is_mutex_held(&dev->dev_lock));
    spin_lock_saved_state_t  irq_state;
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    pcie_config_t*           cfg = dev->cfg;

    /* In order to keep internal bookkeeping coherent, and interactions between
     * MSI/MSI-X and Legacy IRQ mode safe, API users may not directly manipulate
     * the legacy IRQ enable/disable bit.  Just ignore them if they try to
     * manipulate the bit via the modify cmd API. */
    clr_bits &= ~PCIE_CFG_COMMAND_INT_DISABLE;
    set_bits &= ~PCIE_CFG_COMMAND_INT_DISABLE;

    DEBUG_ASSERT(bus_drv && cfg);
    spin_lock_irqsave(&bus_drv->legacy_irq_handler_lock, irq_state);
    pcie_write16(&cfg->base.command, (pcie_read16(&cfg->base.command) & ~clr_bits) | set_bits);
    spin_unlock_irqrestore(&bus_drv->legacy_irq_handler_lock, irq_state);
}

status_t pcie_modify_cmd(pcie_device_state_t* dev, uint16_t clr_bits, uint16_t set_bits) {
    status_t ret;
    MUTEX_ACQUIRE(dev, dev_lock);

    if (dev->plugged_in) {
        pcie_modify_cmd_internal(dev, clr_bits, set_bits);
        ret = NO_ERROR;
    } else {
        ret = ERR_UNAVAILABLE;
    }

    MUTEX_RELEASE(dev, dev_lock);
    return ret;
}

void pcie_rescan_bus(void) {
    pcie_bus_driver_state_t* bus_drv = &g_drv_state;
    pcie_scan_and_start_devices(bus_drv);
}
