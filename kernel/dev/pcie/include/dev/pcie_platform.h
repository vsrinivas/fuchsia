// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <dev/interrupt.h>
#include <dev/pcie_constants.h>
#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

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
 * A structure which holds the state of a block of IRQs allocated by the
 * platform to be used for delivering MSI or MSI-X interrupts.
 */
typedef struct pcie_msi_block {
    void*    platform_ctx; /** Allocation context owned by the platform */
    uint64_t tgt_addr;     /** The target write transaction physical address */
    bool     allocated;    /** Whether or not this block has been allocated */
    uint     base_irq_id;  /** The first IRQ id in the allocated block */
    uint     num_irq;      /** The number of irqs in the allocated block */

    /**
     * The data which the device should write when triggering an IRQ.  Note,
     * only the lower 16 bits are used when the block has been allocated for MSI
     * instead of MSI-X
     */
    uint32_t tgt_data;
} pcie_msi_block_t;

/**
 * Callback definition used for platform-specific legacy IRQ remapping.
 *
 * @param bus_id  The bus ID of the pcie device/bridge to swizzle for.
 * @param dev_id  The device ID of the pcie device/bridge to swizzle for.
 * @param func_id The function ID of the pcie device/bridge to swizzle for.
 * @param pin     The pin we want to swizzle
 * @param irq     An output pointer for what IRQ this pin goes to
 *
 * @return NO_ERROR if we successfully swizzled
 * @return ERR_NOT_FOUND if we did not know how to swizzle this pin
 */
typedef status_t (*platform_legacy_irq_swizzle_t)(uint bus_id,
                                                  uint dev_id,
                                                  uint func_id,
                                                  uint pin,
                                                  uint *irq);

/**
 * Callback definition used for platform allocation of blocks of MSI and MSI-X
 * compatible IRQ targets.
 *
 * @param requested_irqs The total number of irqs being requested.
 * @param can_target_64bit True if the target address of the MSI block can
 *        be located past the 4GB boundary.  False if the target address must be
 *        in low memory.
 * @param is_msix True if this request is for an MSI-X compatible block.  False
 *        for plain old MSI.
 * @param out_block A pointer to the allocation bookkeeping to be filled out
 *        upon successful allocation of the reqested block of IRQs.
 *
 * @return A status code indicating the success or failure of the operation.
 */
typedef status_t (*platform_alloc_msi_block_t)(uint requested_irqs,
                                               bool can_target_64bit,
                                               bool is_msix,
                                               pcie_msi_block_t* out_block);

/**
 * Callback definition used by the bus driver to return a block of MSI IRQs
 * previously allocated with a call to a platform_alloc_msi_block_t
 * implementation to the platform pool.
 *
 * @param block A pointer to the block to be returned.
 */
typedef void (*platform_free_msi_block_t)(pcie_msi_block_t* block);

/**
 * Callback definition used for platform registration of MSI handlers.
 *
 * @param block A pointer to a block of MSIs allocated using a platform supplied
 *        platform_alloc_msi_block_t callback.
 * @param msi_id The ID (indexed from 0) with the block of MSIs to register a
 *        handler for.
 * @param handler A pointer to the handler to register, or NULL to unregister.
 * @param ctx A context pointer to be supplied when the handler is invoked.
 */
typedef void (*platform_register_msi_handler_t)(const pcie_msi_block_t* block,
                                                uint                    msi_id,
                                                int_handler             handler,
                                                void*                   ctx);

/**
 * Callback definition used for platform masking/unmaskingof MSI handlers.
 *
 * @param block A pointer to a block of MSIs allocated using a platform supplied
 *        platform_alloc_msi_block_t callback.
 * @param msi_id The ID (indexed from 0) with the block of MSIs to mask or
 *        unmask.
 * @param mask If true, mask the handler.  Otherwise, unmask it.
 */
typedef void (*platform_mask_unmask_msi_t)(const pcie_msi_block_t* block,
                                           uint                    msi_id,
                                           bool                    mask);


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

/* Returns a pointer to reference init information for the platform.
 * Any NULL fields may be overriden. */
void platform_pcie_init_info(pcie_init_info_t *out);

__END_CDECLS
