// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <assert.h>
#include <dev/interrupt.h>
#include <err.h>
#include <kernel/spinlock.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Fwd decls */
struct pcie_device_state;

/**
 * Enumeration which defines the IRQ modes a PCIe device may be operating in.
 * IRQ modes are exclusive, a device may be operating in only one mode at any
 * given point in time.  Drivers may query the maximum number of IRQs supported
 * by each mode using the pcie_query_irq_mode_capabilities function.  Drivers
 * may request a particular number of IRQs be allocated when selecting an IRQ
 * mode with pcie_set_irq_mode.  IRQ identifiers used in the system when
 * registering, un-registering and dispatching IRQs are on the range [0, N-1]
 * where N are the number of IRQs successfully allocated using a call to
 * pcie_set_irq_mode.
 *
 * ++ PCIE_IRQ_MODE_DISABLED
 *    All IRQs are disabled.  0 total IRQs are supported in this mode.
 *
 * ++ PCIE_IRQ_MODE_LEGACY
 *    Devices may support up to 1 legacy IRQ in total.  Exclusive IRQ access
 *    cannot be guaranteed (the IRQ may be shared with other devices)
 *
 * ++ PCIE_IRQ_MODE_MSI
 *    Devices may support up to 32 MSI IRQs in total.  IRQs may be allocated
 *    exclusively, resources permitting.
 *
 * ++ PCIE_IRQ_MODE_MSI_X
 *    Devices may support up to 2048 MSI-X IRQs in total.  IRQs may be allocated
 *    exclusively, resources permitting.
 */
typedef enum pcie_irq_mode {
    PCIE_IRQ_MODE_DISABLED = 0,
    PCIE_IRQ_MODE_LEGACY   = 1,
    PCIE_IRQ_MODE_MSI      = 2,
    PCIE_IRQ_MODE_MSI_X    = 3,
} pcie_irq_mode_t;

/**
 * A structure used to hold output parameters when calling
 * pcie_query_irq_mode_capabilities
 */
typedef struct pcie_irq_mode_caps {
    uint max_irqs;  /** The maximum number of IRQ supported by the selected mode */
    /**
     * For MSI or MSI-X, indicates whether or not per-vector-masking has been
     * implementd by the hardware.
     */
    bool per_vector_masking_supported;
} pcie_irq_mode_caps_t;

/**
 * An enumeration of the permitted return values from a PCIe IRQ handler.
 *
 * ++ PCIE_IRQRET_NO_ACTION
 *    Do not mask the IRQ, do not request that the kernel perform a reschedule.
 *
 * ++ PCIE_IRQRET_RESCHED
 *    Do not mask the IRQ, request that the kernel perform a reschedule.
 *
 * ++ PCIE_IRQRET_MASK
 *    Mask the IRQ if (and only if) per vector masking is supported, but do not
 *    request that the kernel perform a reschedule.
 *
 * ++ PCIE_IRQRET_MASK_AND_RESCHED
 *    Mask the IRQ if (and only if) per vector masking is supported, and request
 *    that the kernel perform a reschedule.
 */
typedef enum pcie_irq_handler_retval {
    PCIE_IRQRET_NO_ACTION        = 0x0,
    PCIE_IRQRET_RESCHED          = 0x1,
    PCIE_IRQRET_MASK             = 0x2,
    PCIE_IRQRET_MASK_AND_RESCHED = PCIE_IRQRET_RESCHED | PCIE_IRQRET_MASK,
} pcie_irq_handler_retval_t;

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
 * A structure used to hold the details about the currently configured IRQ mode
 * of a device.  Used in conjunction with pcie_get_irq_mode.
 */
typedef struct pcie_irq_mode_info {
   pcie_irq_mode_t          mode;                 /// The currently configured mode.
   uint                     max_handlers;         /// The max number of handlers for the mode.
   uint                     registered_handlers;  /// The current number of registered handlers.
} pcie_irq_mode_info_t;

/**
 * Definition of the callback registered with pcie_register_irq_handler.  This
 * callback will be called by a bus central IRQ dispatcher any time a chosen
 * device IRQ occurs.
 *
 * @note Masked/unmasked status of an IRQ MUST not be manipulated via the API
 * during an IRQ handler dispatch.  If an IRQ needs to be masked as part of a
 * handler's behavior, the appropriate return value should be used instead of in
 * the API.  @see pcie_irq_handler_retval_t
 *
 * @param dev A pointer to the pci device for which this IRQ occurred.
 * @param irq_id The 0-indexed ID of the IRQ which occurred.
 * @param ctx The context pointer registered when registering the handler.
 */
typedef pcie_irq_handler_retval_t (*pcie_irq_handler_fn_t)(struct pcie_device_state* dev,
                                                           uint irq_id,
                                                           void* ctx);

/**
 * Callback definition used for platform-specific legacy IRQ remapping.
 *
 * @param dev A pointer to the pcie device/bridge to swizzle for.
 * @param pin The pin we want to swizzle
 * @param irq An output pointer for what IRQ this pin goes to
 *
 * @return NO_ERROR if we successfully swizzled
 * @return ERR_NOT_FOUND if we did not know how to swizzle this pin
 */
typedef status_t (*platform_legacy_irq_swizzle_t)(const struct pcie_device_state* dev,
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
 * Structure used internally to hold the state of a registered handler.
 */
typedef struct pcie_irq_handler_state {
    spin_lock_t               lock;
    pcie_irq_handler_fn_t     handler;
    void*                     ctx;
    struct pcie_device_state* dev;
    uint                      pci_irq_id;
    bool                      masked;
} pcie_irq_handler_state_t;

/**
 * Query the number of IRQs which are supported for a given IRQ mode by a given
 * device.
 *
 * @param dev A pointer to the pci device to query.
 * @param mode The IRQ mode to query capabilities for.
 * @param out_caps A pointer to structure which, upon success, will hold the
 * capabilities of the selected IRQ mode.
 *
 * @return A status_t indicating the success or failure of the operation.
 */
status_t pcie_query_irq_mode_capabilities(const struct pcie_device_state* dev,
                                          pcie_irq_mode_t mode,
                                          pcie_irq_mode_caps_t* out_caps);

/**
 * Fetch details about the currently configured IRQ mode.
 *
 * @param dev A pointer to the pci device to configure.
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
status_t pcie_get_irq_mode(const struct pcie_device_state* dev,
                           pcie_irq_mode_info_t* out_info);

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
 * @param dev A pointer to the pci device to configure.
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
status_t pcie_set_irq_mode(struct pcie_device_state* dev,
                           pcie_irq_mode_t           mode,
                           uint                      requested_irqs);

/**
 * Set the current IRQ mode to PCIE_IRQ_MODE_DISABLED
 *
 * Convenience function.  @see pcie_set_irq_mode for details.
 */
static inline void pcie_set_irq_mode_disabled(struct pcie_device_state* dev) {
    /* It should be impossible to fail a transition to the DISABLED state,
     * regardless of the state of the system.  ASSERT this in debug builds */
    __UNUSED status_t result;

    result = pcie_set_irq_mode(dev, PCIE_IRQ_MODE_DISABLED, 0);

    DEBUG_ASSERT(result == NO_ERROR);
}

/**
 * Register an IRQ handler for the specified IRQ ID.
 *
 * @param dev A pointer to the pci device to configure.
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
status_t pcie_register_irq_handler(struct pcie_device_state* dev,
                                   uint                      irq_id,
                                   pcie_irq_handler_fn_t     handler,
                                   void*                     ctx);

/**
 * Mask or unmask the specified IRQ for the given device.
 *
 * @param dev A pointer to the pci device to configure.
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
status_t pcie_mask_unmask_irq(struct pcie_device_state* dev,
                              uint                      irq_id,
                              bool                      mask);

/**
 * Mask the specified IRQ for the given device.
 *
 * Convenience function.  @see pcie_mask_unmask_irq for details.
 */
static inline status_t pcie_mask_irq(struct pcie_device_state* dev, uint irq_id) {
    return pcie_mask_unmask_irq(dev, irq_id, true);
}

/**
 * Unmask the specified IRQ for the given device.
 *
 * Convenience function.  @see pcie_mask_unmask_irq for details.
 */
static inline status_t pcie_unmask_irq(struct pcie_device_state* dev, uint irq_id) {
    return pcie_mask_unmask_irq(dev, irq_id, false);
}

__END_CDECLS
