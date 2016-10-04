// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <dev/pcie.h>
#include <dev/pcie_constants.h>
#include <dev/pcie_caps.h>
#include <kernel/mutex.h>
#include <list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>
#include <trace.h>

/*
 * Some notes on locking and reference counting in the PCIe bus driver.
 *
 * PCI/PCIe is organized such that there are up to 256 "buses", each of which can
 * contain up to 32 "devices", each of which can contain up to 8 "functions".  There
 * are special functions (called bridges) which are functions on a bus on the
 * upstream side, and a bus (containing devices) on the downstream side.  A
 * system consists of one or more "root complexes", each of which is a bus.
 * Buses and their bridges form trees, and the root complexes in the system form
 * the system wide forest.
 *
 * The organization of this driver currently attempts to reflect the
 * organization of PCI/PCIe in a system.  There is a single bus driver
 * (pcie_bus_driver_state_t) which owns all of the bus/device/functions in the
 * system. Device/functions are tracked using pcie_device_state_t structures.
 * Bridges are tracked using pcie_bridge_state_t's, which are  C-style
 * subclasses of pcie_device_state_t's.
 *
 * Currently, the driver uses a single pcie_bridge_state_t (called the "host
 * bridge") to track the state of a single root complex in the system.
 * In the future, it needs to be modified to track multiple root complexes, and
 * probably should use a specific type to track root complex state, as root
 * complexes are similar to, but not the same as, bridges.
 *
 * Currently, there are 6 locks used in the driver.  4 of these locks (3 mutexes
 * and one spinlock) are owned by the bus driver.  The other 2 exist in each
 * device instance.  All devices (and therefore, all bridges as well) are
 * ref-counted objects.
 *
 * ----------------------------------
 * ---===== Bus Driver Locks =====---
 * ----------------------------------
 *  ## bus_topology_lock
 *     The topology lock is a mutex used to protect the relationship between
 *     bridges/devices in the system-wide graph of devices.  It is considered to
 *     be a "leaf lock" meaning that no other locks should ever be obtained
 *     while holding the topology lock.  The topology lock must be held any time
 *     changes to edges of the device graph need to be made (the "upstream"
 *     member of pcie_device_state_t and the "downstream" member of
 *     pcie_bridge_state_t).  Traversal of the graph is usually handled using
 *     routines in pcie_topology.c which generally follow a pattern of...
 *
 *     ## Acquire the topology lock.
 *     ## Add a reference to the starting node, assuming that it still exists.
 *        Frequently, this is node is the host bridge.
 *     ## Release the topology lock.
 *     ## Operate on the node of the graph and/or recurse over its children.
 *     ## Release the reference to the node.
 *
 *  ## bus_rescan_lock
 *     The rescan lock is a mutex used to serialize bus rescan operations.  It
 *     is a top level lock which is held across many function calls.  No other
 *     locks may be held when the rescan lock is obtained.  Its sole purpose is
 *     to ensure that no two scan/rescan operations can ever take place at the
 *     same time on different threads.
 *
 *  ## legacy_irq_list_lock
 *     From a device perspective, the legacy IRQ system in PCI uses 4 different
 *     IRQs (INTA - INTD) which were traditionally tied into the system's
 *     interrupt controller(s).  From the system perspective, each INTA - INTD
 *     IRQ can be mapped independently to system interrupt for each device in
 *     each root complex, meaning that there can be either more or less than 4
 *     actual interrupt vectors used by a system to serve the needs of the
 *     legacy IRQ system in PCI.  Legacy IRQs are shared by devices on the PCI
 *     devices, and are dispatched to a central handler managed by the bus
 *     driver.  Interrupt vectors are discovered on demand during the platform
 *     specific remapping step for newly added devices.  The
 *     legacy_irq_list_lock protects the bookkeeping which tracks the list of
 *     discovered interrupt vectors, and the registration of the central
 *     handlers.  It is a mutex, and considered to be leaf-lock (no other locks
 *     should ever be obtained while the legacy_irq_list_lock is being held).
 *
 *  ## legacy_irq_handler_lock
 *     The irq_handler lock is a spinlock used for synchronizing
 *     registration/unregistration operations with legacy IRQ dispatchers.
 *     The lock is held as drivers for devices add and remove handlers to the
 *     shared legacy dispatcher.  It is also held for the duration of a legacy
 *     IRQ dispatch operation, meaning that it is held during callbacks into
 *     kernel mode driver legacy IRQ handlers.  Because of this, it is
 *     critically important that devices make no attempt to register or
 *     unregister an IRQ handler in the context of their registered handler.
 *
 *     TODO(johngro) There is no good reason to have a single irq_handler
 *     lock.  Instead, there should be a separate lock stored in the bookkeeping
 *     for each system interrupt vector.  This would allow for parallel
 *     dispatching of legacy IRQs, provided that the platform mapped the vectors
 *     such that they could be dispatched by multiple cores in parallel.
 *
 * ------------------------------
 * ---===== Device Locks =====---
 * ------------------------------
 *  ## dev_lock
 *     The primary mutex for devices.  The dev_lock serializes API access to
 *     devices from drivers.  It also protects the plugged/unplugged status of
 *     devices, meaning that a system cannot change the plugged status of a
 *     device without obtaining the dev_lock.  As a corollary, API calls are
 *     responsible for verifying the plugged status of a device immediately
 *     after obtaining the dev lock, and failing the API call if the device has
 *     become unplugged.  The dev_lock is neither a leaf lock, nor a top level
 *     lock.  Either of the bus level legacy IRQ locks, or the bus level
 *     topology lock may be obtained while the dev_lock is being held.  Either
 *     the bus_rescan_lock or the device's start/claim lock may be held when the
 *     dev_lock is acquired.
 *
 *  ## start_claim_lock
 *     A device's start_claim_lock is a mutex which protects the started and
 *     claimed status of the device/driver relationship.  In order to claim,
 *     unclaim, start or shutdown a device, the start_claim_lock must first be
 *     held.  The start_claim_lock is almost a top level lock in the system; the
 *     bus level rescan lock is the only lock in the system which may be held
 *     when acquiring a device's start_claim_lock.
 *
 * -------------------------------------------
 * ---===== Device Ref-Counting Rules =====---
 * -------------------------------------------
 *  Devices (and therefor bridges) are ref-counted objects.  While they can be
 *  removed from the device topology spontaneously (see bus_topology_lock),
 *  their state only goes away when their final reference is released.  This
 *  allows drivers to attempt to interact with suddenly removed objects, and
 *  receive a clean error code instead of crashing.  Along with the
 *  bus_topology_lock, it also allows operations to be performed on the entire
 *  device graph without holding an uber-lock for the entire graph-wide
 *  operation.  The rules around ref-counting are as follows...
 *
 *  ## The bus driver holds a reference for the host_bridge device at the root
 *     of the graph.  Eventually, when the code is reworked to be root-complex
 *     oriented, the bus driver will hold a reference for each root-complex
 *     device in each root complex.
 *  ## Bridges hold a reference for each of their downstream children.
 *  ## Children of bridges hold a reference to their upstream bridge.
 *  ## Kernel mode device driver do *not* hold a reference to their device.  The
 *     device will be kept alive by the bus driver, not the device driver.  The
 *     bus driver will ensure that the device driver has been shut down and
 *     disconnected from the device before removing the device from the graph.
 *  ## The user mode device driver dispatchers hold a single reference to their
 *     device.  This reference is added as an effect of a successful call to
 *     pcie_get_nth_device.  No new references may be added.  The user mode
 *     wrapper is responsible for releasing this reference when it shuts down.
 *  ## References are held to a device during the callback of a foreach
 *     operation, but the bus_topology_lock is not, allowing devices to be
 *     removed during foreach operations.  See pcie_topology.c and
 *     pcie_foreach_device.
 *  ## All user facing API operations *must* check to see if a device has become
 *     unplugged while still being reffed, and *must* fail the operation if the
 *     device is unplugged.  An unplugged, but still referenced, device is
 *     disconnected from the graph and will never be revived.  If the same
 *     device gets plugged in again, new bookkeeping data will created for it.
 *     Old bookkeeping will continue to exists until the driver eventually
 *     gets around to releasing the device.
 *  ## Address space resources (IO and MMIO) will be retained, meaning not
 *     returned to the central pool for reallocation,  by unplugged but
 *     referenced devices.  This is to ensure these regions of the IO and
 *     physical bus are not re-used by newly plugged device, as the physical
 *     region used by the zombie device may still be mapped into user-mode
 *     address spaces.
 *
 *  Note:  Manual management of ref-counting in C is a pain.  Effort has been
 *  made to adopt a usage-pattern which will facilitate an eventual transition a
 *  mxtl::RefCounted<T> C++ style pattern for managing ref-counted devices when
 *  the core code is transitioned to C++.
 *
 */
#define LOCK_TRACING 0
#if LOCK_TRACING
#define MUTEX_ACQUIRE(_obj, _lock_name) \
    do { \
        TRACEF("Acquire %s (obj %p lock %p)\n", #_lock_name, (_obj), &(_obj)->_lock_name); \
        mutex_acquire(&(_obj)->_lock_name); \
    } while (0)
#define MUTEX_RELEASE(_obj, _lock_name) \
    do { \
        TRACEF("Release %s (obj %p lock %p)\n", #_lock_name, (_obj), &(_obj)->_lock_name); \
        mutex_release(&(_obj)->_lock_name); \
    } while (0)
#else
#define MUTEX_ACQUIRE(_obj, _lock_name) mutex_acquire(&(_obj)->_lock_name)
#define MUTEX_RELEASE(_obj, _lock_name) mutex_release(&(_obj)->_lock_name)
#endif

typedef struct pcie_kmap_ecam_range {
    pcie_ecam_range_t ecam;
    void*             vaddr;
} pcie_kmap_ecam_range_t;

typedef struct pcie_io_range_alloc {
    pcie_io_range_t  io;
    size_t           used;
} pcie_io_range_alloc_t;

typedef struct pcie_legacy_irq_handler_state {
    pcie_bus_driver_state_t*      bus_drv;
    struct list_node              legacy_irq_list_node;
    struct list_node              device_handler_list;
    uint                          irq_id;
} pcie_legacy_irq_handler_state_t;

struct pcie_bus_driver_state_t {
    pcie_bus_driver_state_t();
    ~pcie_bus_driver_state_t();

    mutex_t                           bus_topology_lock;
    mutex_t                           bus_rescan_lock;
    mxtl::RefPtr<pcie_bridge_state_t> host_bridge;

    vmm_aspace_t*                              aspace;
    mxtl::unique_ptr<pcie_kmap_ecam_range_t[]> ecam_windows;
    size_t                                     ecam_window_count;

    pcie_io_range_alloc_t           mmio_lo;
    pcie_io_range_alloc_t           mmio_hi;
    pcie_io_range_alloc_t           pio;

    platform_legacy_irq_swizzle_t   legacy_irq_swizzle;
    spin_lock_t                     legacy_irq_handler_lock;
    mutex_t                         legacy_irq_list_lock;
    struct list_node                legacy_irq_list;

    platform_alloc_msi_block_t      alloc_msi_block;
    platform_free_msi_block_t       free_msi_block;
    platform_register_msi_handler_t register_msi_handler;
    platform_mask_unmask_msi_t      mask_unmask_msi;
};

/******************************************************************************
 *
 *  pcie.c
 *
 ******************************************************************************/
pcie_bus_driver_state_t* pcie_get_bus_driver_state(void);
void pcie_scan_and_start_devices(pcie_bus_driver_state_t* bus_drv);
void pcie_unplug_device(const mxtl::RefPtr<pcie_device_state_t>& dev);
void pcie_modify_cmd_internal(const mxtl::RefPtr<pcie_device_state_t>& dev,
                              uint16_t clr_bits, uint16_t set_bits);
pcie_config_t* pcie_get_config(const pcie_bus_driver_state_t* bus_drv,
                               uint64_t* cfg_phys,
                               uint bus_id,
                               uint dev_id,
                               uint func_id);

/******************************************************************************
 *
 *  pcie_topology.c
 *
 ******************************************************************************/
typedef bool (*pcie_foreach_device_cbk)(const mxtl::RefPtr<pcie_device_state_t>& dev,
                                        void* ctx, uint level);

void pcie_link_device_to_upstream(const mxtl::RefPtr<pcie_device_state_t>& dev,
                                  const mxtl::RefPtr<pcie_bridge_state_t>& bridge);
void pcie_unlink_device_from_upstream(const mxtl::RefPtr<pcie_device_state_t>& dev);
void pcie_foreach_device(pcie_bus_driver_state_t* drv, pcie_foreach_device_cbk cbk, void* ctx);
mxtl::RefPtr<pcie_device_state_t> pcie_get_refed_device(pcie_bus_driver_state_t* drv,
                                                        uint bus_id, uint dev_id, uint func_id);


/******************************************************************************
 *
 *  pcie_caps.c
 *
 ******************************************************************************/
status_t pcie_parse_capabilities(const mxtl::RefPtr<pcie_device_state_t>& dev);

/******************************************************************************
 *
 *  pcie_irqs.c
 *
 ******************************************************************************/
status_t pcie_query_irq_mode_capabilities_internal(
        const pcie_device_state_t& dev,
        pcie_irq_mode_t mode,
        pcie_irq_mode_caps_t* out_caps);

status_t pcie_get_irq_mode_internal(
        const pcie_device_state_t& dev,
        pcie_irq_mode_info_t* out_info);

status_t pcie_set_irq_mode_internal(
        const mxtl::RefPtr<pcie_device_state_t>& dev,
        pcie_irq_mode_t                          mode,
        uint                                     requested_irqs);

status_t pcie_register_irq_handler_internal(
        const mxtl::RefPtr<pcie_device_state_t>& dev,
        uint                                     irq_id,
        pcie_irq_handler_fn_t                    handler,
        void*                                    ctx);

status_t pcie_mask_unmask_irq_internal(
        const mxtl::RefPtr<pcie_device_state_t>& dev,
        uint                                     irq_id,
        bool                                     mask);

status_t pcie_init_device_irq_state(const mxtl::RefPtr<pcie_device_state_t>& dev,
                                    const mxtl::RefPtr<pcie_bridge_state_t>& upstream);
status_t pcie_init_irqs(pcie_bus_driver_state_t* drv, const pcie_init_info_t* init_info);
void     pcie_shutdown_irqs(pcie_bus_driver_state_t* drv);
