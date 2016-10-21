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
#include <magenta/errors.h>
#include <sys/types.h>

__BEGIN_CDECLS

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

/*
 * Shutdown the PCIe subsystem
 */
void pcie_shutdown(void);

__END_CDECLS

#ifdef __cplusplus

#include <mxtl/ref_counted.h>

class PciePlatformInterface {
public:
    using SwizzleMapEntry = uint32_t[PCIE_MAX_LEGACY_IRQ_PINS];

    virtual ~PciePlatformInterface() { }

    /**
     * Methods used to determine if a platform supports MSI or not, and if so,
     * whether or not the platform can mask individual MSI vectors at the
     * platform level.
     *
     * If the platform supports MSI, it must supply valid implementations of
     * Alloc/FreeMsiBlock, and RegisterMsiHandler.
     *
     * If the platform supports MSI masking, it must supply a valid
     * implementation of MaskUnmaskMsi.
     */
    bool supports_msi() const { return supports_msi_; }
    bool supports_msi_masking() const { return supports_msi_masking_; }

    /**
     * Implemented by platforms which can have dynamic swizzle maps.
     *
     * TODO(johngro) : Get rid of this, it really does not belong in the
     * platform interface.  Legacy swizzling can happen any time an interrupt comes
     * in through a root complex (or root controller in the case of PCI).
     * Swizzling behavior should be a property of these roots (not a global
     * property of the platform) and should be supplied by the platform at the
     * time it adds a root to the bus driver.
     */
    virtual status_t AddLegacySwizzle(uint bus_id,
                                      uint dev_id,
                                      uint func_id,
                                      const SwizzleMapEntry& map_entry) {
        return ERR_NOT_SUPPORTED;
    }

    /**
     * Method used for platform-specific legacy IRQ remapping.  All platforms
     * must implement this.
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
    virtual status_t LegacyIrqSwizzle(uint bus_id,
                                      uint dev_id,
                                      uint func_id,
                                      uint pin,
                                      uint *irq) = 0;

    /**
     * Method used for platform allocation of blocks of MSI and MSI-X compatible
     * IRQ targets.
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
    virtual status_t AllocMsiBlock(uint requested_irqs,
                                   bool can_target_64bit,
                                   bool is_msix,
                                   pcie_msi_block_t* out_block) {
        // Bus driver code should not be calling this if the platform does not
        // indicate support for MSI.
        DEBUG_ASSERT(false);
        return ERR_NOT_SUPPORTED;
    }

    /**
     * Method used by the bus driver to return a block of MSI IRQs previously
     * allocated with a call to a AllocMsiBlock implementation to the platform
     * pool.
     *
     * @param block A pointer to the block to be returned.
     */
    virtual void FreeMsiBlock(pcie_msi_block_t* block) {
        // Bus driver code should not be calling this if the platform does not
        // indicate support for MSI.
        DEBUG_ASSERT(false);
    }

    /**
     * Method used for registration of MSI handlers with the platform.
     *
     * @param block A pointer to a block of MSIs allocated using a platform supplied
     *        platform_alloc_msi_block_t callback.
     * @param msi_id The ID (indexed from 0) with the block of MSIs to register a
     *        handler for.
     * @param handler A pointer to the handler to register, or NULL to unregister.
     * @param ctx A context pointer to be supplied when the handler is invoked.
     */
    virtual void RegisterMsiHandler(const pcie_msi_block_t* block,
                                    uint                    msi_id,
                                    int_handler             handler,
                                    void*                   ctx) {
        // Bus driver code should not be calling this if the platform does not
        // indicate support for MSI.
        DEBUG_ASSERT(false);
    }

    /**
     * Method used for masking/unmaskingof MSI handlers at the platform level.
     *
     * @param block A pointer to a block of MSIs allocated using a platform supplied
     *        platform_alloc_msi_block_t callback.
     * @param msi_id The ID (indexed from 0) with the block of MSIs to mask or
     *        unmask.
     * @param mask If true, mask the handler.  Otherwise, unmask it.
     */
    virtual void MaskUnmaskMsi(const pcie_msi_block_t* block,
                               uint                    msi_id,
                               bool                    mask) {
        // Bus driver code should not be calling this if the platform does not
        // indicate support for MSI masking.
        DEBUG_ASSERT(false);
    }

protected:
    enum class MsiSupportLevel { NONE, MSI, MSI_WITH_MASKING };
    explicit PciePlatformInterface(MsiSupportLevel msi_support)
        : supports_msi_((msi_support == MsiSupportLevel::MSI) ||
                        (msi_support == MsiSupportLevel::MSI_WITH_MASKING)),
          supports_msi_masking_(msi_support == MsiSupportLevel::MSI_WITH_MASKING) { }

private:
    const bool supports_msi_;
    const bool supports_msi_masking_;
};

#endif  // __cplusplus
