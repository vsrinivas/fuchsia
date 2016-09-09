// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <dev/pci.h>
#include <dev/pcie_caps.h>
#include <dev/pcie_constants.h>
#include <dev/pcie_irqs.h>
#include <endian.h>
#include <err.h>
#include <kernel/mutex.h>
#include <sys/types.h>

__BEGIN_CDECLS

typedef struct pcie_config {
    pci_config_t base;
    uint8_t      __pad0[PCIE_BASE_CONFIG_SIZE - sizeof(pci_config_t)];
    uint8_t      extended[PCIE_EXTENDED_CONFIG_SIZE - PCIE_BASE_CONFIG_SIZE];
} __PACKED pcie_config_t;

/* Fwd decls */
struct pcie_bridge_state;
struct pcie_bus_driver_state;
struct pcie_device_state;
struct pcie_driver_registration;
struct pcie_legacy_irq_handler_state;

/*
 * struct used to fetch information about a configured base address register
 */
typedef struct pcie_bar_info {
    struct pcie_device_state* dev;

    uint64_t size;
    uint64_t bus_addr;
    bool     is_mmio;
    bool     is_64bit;
    bool     is_prefetchable;
    uint     first_bar_reg;
    bool     is_allocated;
} pcie_bar_info_t;

/**
 * A struct used to describe a sub-range of the address space of one of the
 * system buses.  Typically, this is a range of the main system bus, but it
 * might also be the I/O space bus on an architecture like x86/x64
 *
 * @param bus_addr The base address of the I/O range on the appropriate bus.
 * For MMIO or memory mapped config, this will be an address on the main system
 * bus.  For PIO regions, this may also be a an address on the main system bus
 * for architectures which do not have a sepearate I/O bus (ARM, MIPS, etc..).
 * For systems which do have a separate I/O bus (x86/x64) this should be the
 * base address in I/O space.
 *
 * @param size The size of the range in bytes.
 */
typedef struct pcie_io_range {
    uint64_t bus_addr;
    size_t size;
} pcie_io_range_t;

/**
 * A struct used to describe a range of the Extended Configuration Access
 * Mechanism (ECAM) region.
 *
 * @param io_range The MMIO range which describes the region of the main system
 * bus where this slice of the ECAM resides.
 * @param bus_start The ID of the first bus covered by this slice of ECAM.
 * @param bus_end The ID of the last bus covered by this slice of ECAM.
 */
typedef struct pcie_ecam_range {
    pcie_io_range_t io_range;
    uint8_t bus_start;
    uint8_t bus_end;
} pcie_ecam_range_t;

/**
 * A struct used to describe the resources to be used by the PCIe subsystem for
 * discovering and configuring PCIe controllers, bridges and devices.
 */
typedef struct pcie_init_info {
    /**
     * A pointer to an array of pcie_ecam_range_t structures which describe the
     * ECAM regions available to the subsytem.  The windows must...
     * -# Be listed in ascending bus_start order.
     * -# Contain a range which describes Bus #0
     * -# Consist of non-overlapping [bus_start, bus_end] ranges.
     * -# Have a sufficiently sized IO range to contain the configuration
     *    structures for the given bus range.  Each bus requries 4KB * 8
     *    functions * 32 devices worth of config space.
     */
    const pcie_ecam_range_t* ecam_windows;

    /** The number of elements in the ecam_windows array. */
    size_t ecam_window_count;

    /**
     * The low-memory region of MMIO space.  The physical addresses for the
     * range must exist entirely below the 4GB mark on the system bus.  32-bit
     * MMIO regions described by device BARs must be allocated from this window.
     */
    pcie_io_range_t mmio_window_lo;

    /**
     * The high-memory region of MMIO space.  This range is optional; set
     * mmio_window_hi.size to zero if there is no high memory range on this
     * system.  64-bit MMIO regions described by device BARs will be
     * preferentally allocated from this window.
     */
    pcie_io_range_t mmio_window_hi;

    /**
     * The PIO space.  On x86/x64 systems, this will describe the regions of the
     * 16-bit IO address space which are availavle to be allocated to PIO BARs
     * for PCI devices.  On other systems, this describes the physical address
     * space that the system reserves for producing PIO cycles on PCI.  Note;
     * this region must exist in low memory (below the 4GB mark)
     */
    pcie_io_range_t pio_window;

    /** Platform-specific legacy IRQ remapping.  @see platform_legacy_irq_swizzle_t */
    platform_legacy_irq_swizzle_t legacy_irq_swizzle;

    /**
     * Routines for allocating and freeing blocks of IRQs for use with MSI or
     * MSI-X, and for registering handlers for IRQs within blocks.  May be NULL
     * if the platform's interrupts controller is not compatible with MSI.
     * @note Either all of these routines must be provided, or none of them.
     */
    platform_alloc_msi_block_t      alloc_msi_block;
    platform_free_msi_block_t       free_msi_block;
    platform_register_msi_handler_t register_msi_handler;

    /**
     * Routine for masking/unmasking MSI IRQ handlers.  May be NULL if the
     * platform is incapable of masking individual MSI handlers.
     */
    platform_mask_unmask_msi_t mask_unmask_msi;
} pcie_init_info_t;

/* Function table registered by a device driver.  Method requirements and device
 * lifecycle are described below.
 *
 * + pcie_probe_fn
 *   Called by the bus driver during bus scanning/probing to determine which
 *   registered driver (if any) wishes to claim and manage a device.  Drivers
 *   who wish to claim a device must return a non-NULL void* context pointer
 *   which will be made available as the driver_ctx member of the
 *   pcie_device_state_t structure and provided to subsequent callbacks via
 *   the pci_device member.
 *
 * + startup_hook
 *   Called by the bus driver in order to start a device after it has been
 *   claimed.  All MMIO/PIO registers will be allocated, but un-mapped in at the
 *   time the startup hook is invoked, and the device IRQ will be masked.
 *   Devices should not enable their IRQ during startup.  Device IRQs will be
 *   automatically enabled at the PCI level following a successful startup if a
 *   device has registered an IRQ hook.
 *
 * + shutdown_hook
 *   Called by the bus driver on a successfully started device when it is time
 *   to shut down.  Device registers are guaranteed to be mapped when shutdown
 *   is called.  Shutdown will not be called for devices who fail to start-up,
 *   so devices which encounter problems during start-up should take care to
 *   leave their device in a quiescent state before returning their error code
 *   to the bus driver.  Devices may use pcie_enable_irq to mask their IRQ and
 *   synchronize with the bus's IRQ dispatcher at the appropriate point in their
 *   shutdown sequence.
 *
 * + release_hook
 *   Called on a non-started device when it is time to release any resources
 *   which may have been allocated during its life cycle.  At a minimum, drivers
 *   who dynamically allocate context during pcie_probe_fn should register a
 *   release_hook in order to clean up their dynamically allocated resources.  A
 *   driver's release hook will always be called if the driver attempted to
 *   claim a device during probe.  Note that it is possible that the device was
 *   never started, or possibly never even claimed (due to hotplug or
 *   multithreaded races).  Drivers should use the only as a chance to free any
 *   internal state associated with an attempt to claim a device.
 */
typedef struct pcie_driver_fn_table {
    void*               (*pcie_probe_fn)   (struct pcie_device_state* pci_device);
    status_t            (*pcie_startup_fn) (struct pcie_device_state* pci_device);
    void                (*pcie_shutdown_fn)(struct pcie_device_state* pci_device);
    void                (*pcie_release_fn) (void* ctx);
} pcie_driver_fn_table_t;

typedef struct pcie_driver_registration {
    const char*                   name;
    const pcie_driver_fn_table_t* fn_table;
} pcie_driver_registration_t;

/*
 * Struct used to manage the relationship between a PCIe device/function and its
 * associated driver.  During a bus scan/probe operation, all drivers will have
 * their registered probe methods called until a driver claims a device.  A
 * driver may claim a device by returning a pointer to a driver-managed
 * pcie_device_state struct, with the driver owned fields filled out.
 */
typedef struct pcie_device_state {
    pcie_config_t*                cfg;       // Pointer to the memory mapped ECAM (kernel vaddr)
    paddr_t                       cfg_phys;  // The physical address of the device's ECAM
    struct pcie_bridge_state*     upstream;  // The upstream bridge, or NULL if we are root
    struct pcie_bus_driver_state* bus_drv;   // Pointer to our bus driver state.
    bool                          is_bridge; // True if this device is also a bridge
    uint16_t                      vendor_id; // The device's vendor ID, as read from config
    uint16_t                      device_id; // The device's device ID, as read from config
    uint8_t                       class_id;  // The device's class ID, as read from config.
    uint8_t                       subclass;  // The device's subclass, as read from config.
    uint8_t                       prog_if;   // The device's programming interface (from cfg)
    uint                          bus_id;    // The bus ID this bridge/device exists on
    uint                          dev_id;    // The device ID of this bridge/device
    uint                          func_id;   // The function ID of this bridge/device

    /* State related to lifetime management */
    int                           ref_count;
    mutex_t                       dev_lock;
    mutex_t                       start_claim_lock;
    bool                          plugged_in;

    /* State tracking for this device's driver (if this device has been claimed by a driver) */
    const pcie_driver_registration_t* driver;
    void*                             driver_ctx;
    bool                              started;

    /* Info about the BARs computed and cached during the initial setup/probe,
     * indexed by starting BAR register index */
    pcie_bar_info_t bars[PCIE_MAX_BAR_REGS];

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
        pcie_irq_mode_t           mode;
        pcie_irq_handler_state_t  singleton_handler;
        pcie_irq_handler_state_t* handlers;
        uint                      handler_count;
        uint                      registered_handler_count;

        /* Legacy IRQ state */
        struct {
            uint8_t pin;
            struct list_node shared_handler_node;
            struct pcie_legacy_irq_handler_state* shared_handler;
        } legacy;

        /* MSI state */
        struct {
            pcie_cap_msi_t*    cfg;
            uint               max_irqs;
            bool               is64bit;
            volatile uint32_t* pvm_mask_reg;
            pcie_msi_block_t   irq_block;
        } msi;

        /* TODO(johngro) : Add MSI-X state */
        struct { } msi_x;
    } irq;

} pcie_device_state_t;

typedef struct pcie_bridge_state {
    pcie_device_state_t dev;             // Common device state for this bridge.
    uint                managed_bus_id;  // The ID of the downstream bus which this bridge manages.

    /* An array of pointers for all the possible functions which exist on the
     * downstream bus of this bridge.  Note: in the special case of the root
     * host bridge, the function pointer will always be NULL in order to avoid
     * cycles in the graph.
     *
     * TODO(johngro): This would be more efficient if it were kept as a set<>
     * sorted by bus/dev/func identifiers.  Switch this when we fully transition
     * to C++ and Magenta.
     */
    pcie_device_state_t* downstream[PCIE_MAX_FUNCTIONS_PER_BUS];
} pcie_bridge_state_t;

/*
 * Endian independent PCIe register access helpers.
 */
static inline uint8_t  pcie_read8 (const volatile uint8_t*  reg) { return *reg; }
static inline uint16_t pcie_read16(const volatile uint16_t* reg) { return LE16(*reg); }
static inline uint32_t pcie_read32(const volatile uint32_t* reg) { return LE32(*reg); }

static inline void pcie_write8 (volatile uint8_t*  reg, uint8_t  val) { *reg = val; }
static inline void pcie_write16(volatile uint16_t* reg, uint16_t val) { *reg = LE16(val); }
static inline void pcie_write32(volatile uint32_t* reg, uint32_t val) { *reg = LE32(val); }

/*
 * Helper methods used for safely downcasting from pcie_device_state_t's to
 * their derrived types.
 */
static inline pcie_bridge_state_t* pcie_downcast_to_bridge(const pcie_device_state_t* device) {
    DEBUG_ASSERT(device);
    return device->is_bridge ? containerof(device, pcie_bridge_state_t, dev) : NULL;
}

/*
 * Init the PCIe subsystem
 *
 * @param init_info A pointer to the information describing the resources to be
 * used by the bus driver to access the PCIe subsystem on this platform.  See \p
 * struct pcie_init_info for more details.
 *
 * @return NO_ERROR if everything goes well.
 */
status_t pcie_init(const pcie_init_info_t* init_info);

/*
 * Shutdown the PCIe subsystem
 */
void pcie_shutdown(void);

/**
 * Fetches a ref'ed pointer to the Nth PCIe device currently in the system.
 * Used for iterating through all PCIe devices.
 *
 * @param index The 0-based index of the device to fetch.
 *
 * @return A ref'ed pointer the requested device, or NULL if no such device
 * exists.  @note If a pointer to a device is returned, it must eventually be
 * released using a call to pcie_release_device.
 */
pcie_device_state_t* pcie_get_nth_device(uint32_t index);

/**
 * Release a device previously obtained using pcie_get_nth_device.
 */
void pcie_release_device(pcie_device_state_t* dev);

/*
 * Attaches a driver to a PCI device. Returns ERR_ALREADY_BOUND if the device has already been
 * claimed by another driver.
 */
status_t pcie_claim_and_start_device(pcie_device_state_t* device,
                                     const pcie_driver_registration_t* driver,
                                     void* driver_ctx);

/*
 * Shutdown and unclaim a device had been successfully claimed with
 * pcie_claim_and_start_device()
 */
void pcie_shutdown_device(pcie_device_state_t* device);

/*
 * Trigger a function level reset (if possible)
 */
status_t pcie_do_function_level_reset(pcie_device_state_t* dev);

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
static inline const pcie_bar_info_t* pcie_get_bar_info(const pcie_device_state_t* dev,
                                                       uint bar_ndx) {
    DEBUG_ASSERT(dev);
    DEBUG_ASSERT(bar_ndx < countof(dev->bars));

    const pcie_bar_info_t* ret = &dev->bars[bar_ndx];
    return ret->is_allocated ? ret : NULL;
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
status_t pcie_modify_cmd(pcie_device_state_t* device, uint16_t clr_bits, uint16_t set_bits);

/*
 * Enable or disable bus mastering in a device's configuration.
 *
 * @param device A pointer to the target device.
 * @param enable If true, allow the device to access main system memory as a bus
 * master.
 * @return A status_t indicating success or failure of the operation.
 */
static inline status_t pcie_enable_bus_master(pcie_device_state_t* device, bool enabled) {
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
static inline status_t pcie_enable_pio(pcie_device_state_t* device, bool enabled) {
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
static inline status_t pcie_enable_mmio(pcie_device_state_t* device, bool enabled) {
    return pcie_modify_cmd(device,
                           enabled ? 0 : PCI_COMMAND_MEM_EN,
                           enabled ? PCI_COMMAND_MEM_EN : 0);
}

/*
 * Simple inline helper which fetches a device driver's name, or substitutes
 * "<unknown>" if the driver didn't supply a name, or for some mysterious
 * reason, is NULL.
 */
static inline const char* pcie_driver_name(const pcie_driver_registration_t* driver) {
    return (driver && driver->name) ? driver->name : "<unknown>";
}

#if WITH_DEV_PCIE
#define STATIC_PCIE_DRIVER(var_name, drv_name, drv_fn_table)           \
    extern const pcie_driver_registration_t __pcie_drv_reg_##var_name; \
    const pcie_driver_registration_t __pcie_drv_reg_##var_name         \
    __ALIGNED(sizeof(void *)) __SECTION("pcie_builtin_drivers") =     \
    {                                                                  \
        .name           = drv_name,                                    \
        .fn_table       = &drv_fn_table,                               \
    };
#else  // WITH_DEV_PCIE
#define STATIC_PCIE_DRIVER(var_name, drv_name, drv_fn_table)
#endif  // WITH_DEV_PCIE

/**
 * Temporary hack; do not use!
 */
void pcie_rescan_bus(void);

/* Returns a pointer to reference init information for the platform.
 * Any NULL fields may be overriden. */
void platform_pcie_init_info(pcie_init_info_t *out);

__END_CDECLS
