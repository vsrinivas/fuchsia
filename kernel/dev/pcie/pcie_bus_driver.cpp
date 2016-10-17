// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie.h>
#include <dev/pcie_bus_driver.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <new.h>
#include <trace.h>

#include "pcie_priv.h"

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
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_SLAB_SIZE;
constexpr size_t PcieBusDriver::REGION_BOOKKEEPING_MAX_MEM;

mxtl::RefPtr<PcieBusDriver> PcieBusDriver::driver_;
Mutex PcieBusDriver::driver_lock_;

PcieBusDriver::PcieBusDriver() {
}

PcieBusDriver::~PcieBusDriver() {
    /* Force shutdown any devices which happen to still be running. */
    ForeachDevice([](const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level) -> bool {
                      DEBUG_ASSERT(dev);
                      pcie_shutdown_device(dev);
                      return true;
                  }, nullptr);

    /* Shut off all of our IRQs and free all of our bookkeeping */
    ShutdownIrqs();

    /* Free the device tree */
    if (root_complex_ != nullptr) {
        root_complex_->Unplug();
        root_complex_.reset();
    }

    // Release the region bookkeeping memory.
    region_bookkeeping_.reset();

    // Free the ECAM window bookkeeping memory, unmapping anything we have
    // mapped into kernel VM as we go.
    if (ecam_windows_) {
        DEBUG_ASSERT(aspace_);

        for (size_t i = 0; i < ecam_window_count_; ++i) {
            auto& window = ecam_windows_[i];
            if (window.vaddr)
                vmm_free_region(aspace_, (vaddr_t)window.vaddr);
        }

        ecam_windows_ = nullptr;
    }
}

status_t PcieBusDriver::Start(const pcie_init_info_t* init_info) {
    status_t status = NO_ERROR;
    AllocChecker ac;

    if (!init_info) {
        TRACEF("Failed to start PCIe bus driver; no init info provided");
        return ERR_INVALID_ARGS;
    }

    if (started_)  {
        TRACEF("Failed to start PCIe bus driver; driver already started\n");
        return ERR_BAD_STATE;
    }

    /* Initialize our state */
    status = InitIrqs(init_info);
    if (status != NO_ERROR) {
        goto bailout;
    }

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
    if (((init_info->mmio_window_lo.bus_addr + 0ull) >= 0x100000000ull) ||
        ((init_info->mmio_window_lo.size     + 0ull) >= 0x100000000ull) ||
         (init_info->mmio_window_lo.bus_addr > (0x100000000ull - init_info->mmio_window_lo.size))) {
        TRACEF("Low mem MMIO region [%zx, %zx) does not exist entirely below 4GB mark.\n",
                (size_t)init_info->mmio_window_lo.bus_addr,
                (size_t)init_info->mmio_window_lo.bus_addr + init_info->mmio_window_lo.size);
        goto bailout;
    }

    /* The PIO region must fit somewhere in the architecture's I/O bus region */
    if (((init_info->pio_window.bus_addr + 0ull) >= PCIE_PIO_ADDR_SPACE_SIZE) ||
        ((init_info->pio_window.size     + 0ull) >= PCIE_PIO_ADDR_SPACE_SIZE) ||
         (init_info->pio_window.bus_addr > (PCIE_PIO_ADDR_SPACE_SIZE - init_info->pio_window.size)))
    {
        TRACEF("PIO region [%zx, %zx) too large for architecture's PIO address space size (%zx)\n",
                (size_t)init_info->pio_window.bus_addr,
                (size_t)init_info->pio_window.bus_addr + init_info->pio_window.size,
                (size_t)PCIE_PIO_ADDR_SPACE_SIZE);
        goto bailout;
    }

    /* Init parameters look good, go back to assuming NO_ERROR for now. */
    status = NO_ERROR;

    /* Configure the RegionAllocators for the root complex using the regions of
     * the MMIO and PIO busses provided by platform to the allocators */
    {
        struct {
            RegionAllocator& alloc;
            const pcie_io_range_t& range;
        } ALLOC_INIT[] = {
            { .alloc = root_complex_->mmio_lo_regions, .range = init_info->mmio_window_lo },
            { .alloc = root_complex_->mmio_hi_regions, .range = init_info->mmio_window_hi },
            { .alloc = root_complex_->pio_regions,     .range = init_info->pio_window },
        };
        for (const auto& iter : ALLOC_INIT) {
            if (iter.range.size) {
                status = iter.alloc.AddRegion({ .base = iter.range.bus_addr,
                                                .size = iter.range.size });
                if (status != NO_ERROR) {
                    TRACEF("Failed to initilaize region allocator (0x%#" PRIx64 ", size 0x%zx\n",
                           iter.range.bus_addr, iter.range.size);
                    goto bailout;
                }
            }
        }
    }

    /* Stash the ECAM window info and map the ECAM windows into the bus driver's
     * address space so we can access config space. */
    ecam_window_count_ = init_info->ecam_window_count;
    ecam_windows_.reset(new (&ac) KmapEcamRange[ecam_window_count_]);
    if (!ac.check()) {
        TRACEF("Failed to initialize PCIe bus driver; could not allocate %zu "
               "bytes for ECAM window state\n",
               ecam_window_count_ * sizeof(ecam_windows_[0]));
        status = ERR_NO_MEMORY;
        goto bailout;
    }

    /* TODO(johngro) : Don't grab a reference to the kernel's address space.
     * What we want is a reference to our current address space (The one that
     * the bus driver will run in). */
    DEBUG_ASSERT(!aspace_);
    aspace_ = vmm_get_kernel_aspace();
    if (aspace_ == NULL) {
        TRACEF("Failed to initialize PCIe bus driver; could not obtain handle "
               "to kernel address space\n");
        status = ERR_INTERNAL;
        goto bailout;
    }

    for (size_t i = 0; i < ecam_window_count_; ++i) {
        auto& window = ecam_windows_[i];
        auto& ecam   = window.ecam;
        char name_buf[32];

        ecam = init_info->ecam_windows[i];

        if ((ecam.io_range.size     & ((size_t)PAGE_SIZE   - 1)) ||
            (ecam.io_range.bus_addr & ((uint64_t)PAGE_SIZE - 1))) {
            TRACEF("Failed to initialize PCIe bus driver; Invalid ECAM window "
                   "%#zx @ %#" PRIx64 ").  Windows must be page aligned and a "
                   "multiple of pages in length.\n",
                   ecam.io_range.size, ecam.io_range.bus_addr);
            status = ERR_INVALID_ARGS;
            goto bailout;
        }

        snprintf(name_buf, sizeof(name_buf), "pcie_cfg%zu", i);
        name_buf[sizeof(name_buf) - 1] = 0;

        DEBUG_ASSERT(ecam.io_range.bus_addr <= mxtl::numeric_limits<paddr_t>::max());

        status = vmm_alloc_physical(
                aspace_,
                name_buf,
                ecam.io_range.size,
                &window.vaddr,
                PAGE_SIZE_SHIFT,
                0 /* min alloc gap */,
                static_cast<paddr_t>(ecam.io_range.bus_addr),
                0 /* vmm flags */,
                ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                    ARCH_MMU_FLAG_PERM_WRITE);
        if (status != NO_ERROR) {
            TRACEF("Failed to initialize PCIe bus driver; Failed to map ECAM window "
                   "(%#zx @ %#" PRIx64 ").  Status = %d.\n",
                   ecam.io_range.size, ecam.io_range.bus_addr, status);
            goto bailout;
        }
    }

    /* Scan the bus and start up any drivers who claim devices we discover */
    ScanAndStartDevices();
    started_ = true;

bailout:
    if (status != NO_ERROR)
        PcieBusDriver::ShutdownDriver();

    return status;
}

pcie_config_t* PcieBusDriver::GetConfig(uint64_t* cfg_phys,
                                        uint bus_id,
                                        uint dev_id,
                                        uint func_id) const {
    DEBUG_ASSERT(bus_id  < PCIE_MAX_BUSSES);
    DEBUG_ASSERT(dev_id  < PCIE_MAX_DEVICES_PER_BUS);
    DEBUG_ASSERT(func_id < PCIE_MAX_FUNCTIONS_PER_DEVICE);

    for (size_t i = 0; i < ecam_window_count_; ++i) {
        const auto& window = ecam_windows_[i];
        const auto& ecam   = window.ecam;

        if ((bus_id >= ecam.bus_start) && (bus_id <= ecam.bus_end)) {
            size_t offset;

            bus_id -= ecam.bus_start;
            offset = (((size_t)bus_id)  << 20) |
                     (((size_t)dev_id)  << 15) |
                     (((size_t)func_id) << 12);

            DEBUG_ASSERT(window.vaddr);
            DEBUG_ASSERT(ecam.io_range.size >= PCIE_EXTENDED_CONFIG_SIZE);
            DEBUG_ASSERT(offset <= (ecam.io_range.size - PCIE_EXTENDED_CONFIG_SIZE));

            if (cfg_phys)
                *cfg_phys = window.ecam.io_range.bus_addr + offset;

            return reinterpret_cast<pcie_config_t*>(static_cast<uint8_t*>(window.vaddr) + offset);
        }
    }

    if (cfg_phys)
        *cfg_phys = 0;

    return nullptr;
}

mxtl::RefPtr<pcie_device_state_t> PcieBusDriver::GetNthDevice(uint32_t index) {
    struct GetNthDeviceState {
        uint32_t index;
        mxtl::RefPtr<pcie_device_state_t> ret;
    } state;

    state.index = index;

    ForeachDevice(
        [](const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level) -> bool {
            DEBUG_ASSERT(dev && ctx);

            auto state = reinterpret_cast<GetNthDeviceState*>(ctx);
            if (!state->index) {
                state->ret = dev;
                return false;
            }

            state->index--;
            return true;
        }, &state);

    return mxtl::move(state.ret);
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
uint PcieBusDriver::MapPinToIrq(const pcie_device_state_t* dev,
                                const pcie_bridge_state_t* upstream) {
    DEBUG_ASSERT(dev->cfg);
    DEBUG_ASSERT(dev->irq.legacy.pin <= PCIE_MAX_LEGACY_IRQ_PINS);
    DEBUG_ASSERT(dev->irq.legacy.pin);
    uint pin = dev->irq.legacy.pin - 1;  // Change to 0s indexing

    /* Hold the bus topology lock while we do this, so we don't need to worry
     * about stuff disappearing as we walk the tree up */
    {
        AutoLock lock(bus_topology_lock_);

        /* Walk up the PCI/PCIe tree, applying the swizzling rules as we go.  Stop
         * when we reach the device which is hanging off of the root bus/root
         * complex.  At this point, platform specific swizzling takes over.
         */
        DEBUG_ASSERT(upstream);  // We should not be mapping IRQs for the "host bridge" special case
        while (upstream->upstream) {
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
            switch (upstream->pcie_caps.devtype) {
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
            dev = static_cast<const pcie_device_state_t*>(upstream);
            upstream = upstream->upstream.get();
        }
    }   // Leave bus_topology_lock_

    uint irq;
    __UNUSED status_t status;
    DEBUG_ASSERT(platform_.legacy_irq_swizzle);
    status = platform_.legacy_irq_swizzle(dev->bus_id, dev->dev_id, dev->func_id, pin, &irq);
    DEBUG_ASSERT(status == NO_ERROR);

    return irq;
}

void PcieBusDriver::LinkDeviceToUpstream(pcie_device_state_t& dev, pcie_bridge_state_t& bridge) {
    AutoLock lock(bus_topology_lock_);

    /* Have the device hold a reference to its upstream bridge. */
    DEBUG_ASSERT(dev.upstream == nullptr);
    dev.upstream = mxtl::WrapRefPtr(&bridge);

    /* Have the bridge hold a reference to the device */
    uint ndx = (dev.dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id;
    DEBUG_ASSERT(ndx < countof(bridge.downstream));
    DEBUG_ASSERT(bridge.downstream[ndx] == nullptr);
    bridge.downstream[ndx] = mxtl::WrapRefPtr(&dev);
}

void PcieBusDriver::UnlinkDeviceFromUpstream(pcie_device_state_t& dev) {
    AutoLock lock(bus_topology_lock_);

    if (dev.upstream != nullptr) {
        uint ndx = (dev.dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev.func_id;
        DEBUG_ASSERT(ndx < countof(dev.upstream->downstream));
        DEBUG_ASSERT(&dev == dev.upstream->downstream[ndx].get());

        /* Let go of the upstream's reference to the device */
        dev.upstream->downstream[ndx] = nullptr;

        /* Let go of the device's reference to its upstream */
        dev.upstream = nullptr;
    }
}

mxtl::RefPtr<pcie_bridge_state_t> PcieBusDriver::GetUpstream(pcie_device_state_t& dev) {
    AutoLock lock(bus_topology_lock_);
    auto ret = dev.upstream;
    return ret;
}

mxtl::RefPtr<pcie_device_state_t> PcieBusDriver::GetDownstream(pcie_bridge_state_t& bridge,
                                                               uint ndx) {
    DEBUG_ASSERT(ndx <= countof(bridge.downstream));
    AutoLock lock(bus_topology_lock_);
    auto ret = bridge.downstream[ndx];
    return ret;
}

mxtl::RefPtr<pcie_device_state_t> PcieBusDriver::GetRefedDevice(uint bus_id,
                                                                uint dev_id,
                                                                uint func_id) {
    struct GetRefedDeviceState {
        uint bus_id;
        uint dev_id;
        uint func_id;
        mxtl::RefPtr<pcie_device_state_t> ret;
    } state;

    state.bus_id  = bus_id,
    state.dev_id  = dev_id,
    state.func_id = func_id,

    ForeachDevice(
            [](const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level) -> bool {
                DEBUG_ASSERT(dev && ctx);
                auto state = reinterpret_cast<GetRefedDeviceState*>(ctx);

                if ((state->bus_id  == dev->bus_id) &&
                    (state->dev_id  == dev->dev_id) &&
                    (state->func_id == dev->func_id)) {
                    state->ret = dev;
                    return false;
                }

                return true;
            }, &state);

    return mxtl::move(state.ret);
}

void PcieBusDriver::ForeachDevice(ForeachCallback cbk, void* ctx) {
    DEBUG_ASSERT(cbk);

    // Grab a reference to the root complex if we can
    AutoLock lock(bus_topology_lock_);
    auto root_complex = root_complex_;
    lock.release();

    if (root_complex == nullptr) {
        TRACEF("No root complex!");
        return;
    }

    ForeachDeviceOnBridge(root_complex, 0, cbk, ctx);
}

status_t PcieBusDriver::AllocBookkeeping() {
    /* Create the RegionPool we will use to supply the memory for the
     * bookkeeping for all of our region tracking and allocation needs.
     * Then assign it to each of our allocators. */
    region_bookkeeping_ = RegionAllocator::RegionPool::Create(REGION_BOOKKEEPING_SLAB_SIZE,
                                                              REGION_BOOKKEEPING_MAX_MEM);
    if (region_bookkeeping_ == nullptr) {
        TRACEF("Failed to create pool allocator for Region bookkeeping!\n");
        return ERR_NO_MEMORY;
    }

    /* Allocate the root complex, currently modeled as a bridge.
     *
     * TODO(johngro) : refactor this.  PCIe root complexes are neither bridges
     * nor devices.  They do not have BDF address, base address registers,
     * configuration space, etc...
     */
    AllocChecker ac;
    root_complex_ = mxtl::AdoptRef(new (&ac) pcie_bridge_state_t(*this, 0));
    if (!ac.check()) {
        TRACEF("Failed to allocate root complex\n");
        return ERR_NO_MEMORY;
    }

    return NO_ERROR;
}

void PcieBusDriver::ScanAndStartDevices() {
    AutoLock lock(bus_rescan_lock_);

    /* Scan the root complex looking for for devices and other bridges. */
    DEBUG_ASSERT(root_complex_ != nullptr);
    pcie_scan_bus(root_complex_);

    /* Attempt to allocate any unallocated BARs */
    pcie_allocate_downstream_bars(root_complex_);

    /* Go over our tree and look for drivers who might want to take ownership of
     * devices. */
    ForeachDevice(pcie_claim_devices_helper, nullptr);

    /* Give the devices claimed by drivers a chance to start */
    ForeachDevice(
            [](const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level) {
                DEBUG_ASSERT(dev);

                /* Don't let the started/claimed status of the device change for the
                 * duration of this operaion */
                AutoLock start_claim_lock(dev->start_claim_lock);
                pcie_start_device(dev);

                return true;
            }, nullptr);
}

bool PcieBusDriver::ForeachDeviceOnBridge(const mxtl::RefPtr<pcie_bridge_state_t>& bridge,
                                          uint                                     level,
                                          ForeachCallback                          cbk,
                                          void*                                    ctx) {
    DEBUG_ASSERT(bridge && cbk);
    bool keep_going = true;

    for (size_t i = 0; keep_going && (i < countof(bridge->downstream)); ++i) {
        AutoLock lock(bus_topology_lock_);
        auto dev = bridge->downstream[i];
        lock.release();

        if (!dev)
            continue;

        keep_going = cbk(dev, ctx, level);

        /* It should be impossible to have a bridge topology such that we could
         * recurse more than 256 times. */
        if (keep_going && (level < 256)) {
            auto downstream_bridge = dev->DowncastToBridge();
            if (downstream_bridge)
                keep_going = ForeachDeviceOnBridge(downstream_bridge, level + 1, cbk, ctx);
        }
    }

    return keep_going;
}


status_t PcieBusDriver::InitializeDriver() {
    AutoLock lock(driver_lock_);

    if (driver_ != nullptr) {
        TRACEF("Failed to initialize PCIe bus driver; driver already initialized\n");
        return ERR_BAD_STATE;
    }

    AllocChecker ac;
    driver_ = mxtl::AdoptRef(new (&ac) PcieBusDriver());
    if (!ac.check()) {
        TRACEF("Failed to allocate PCIe bus driver\n");
        return ERR_NO_MEMORY;
    }

    status_t ret = driver_->AllocBookkeeping();
    if (ret != NO_ERROR)
        driver_.reset();

    return ret;
}

void PcieBusDriver::ShutdownDriver() {
    mxtl::RefPtr<PcieBusDriver> driver;

    {
        AutoLock lock(driver_lock_);
        driver = mxtl::move(driver_);
    }

    driver.reset();
}

static void pcie_driver_init_hook(uint level) {
    status_t res = PcieBusDriver::InitializeDriver();
    if (res != NO_ERROR)
        TRACEF("Failed to initialize PCIe bus driver module! (res = %d)\n", res);
}

LK_INIT_HOOK(pcie_driver_init, pcie_driver_init_hook, LK_INIT_LEVEL_PLATFORM - 1);
