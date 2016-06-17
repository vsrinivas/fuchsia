// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <assert.h>
#include <dev/pcie.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <dev/broadwell_chipset_config.h>
#include <platform.h>
#include <sys/types.h>
#include <string.h>
#include <trace.h>

/* Chipset configuration registers (as well as a ton of other registers) are all
 * hidden in various places underneath the Very Special 0:31.0 BDF function in
 * PCI.  The device class/subclass indicates that it is an ISA bridge, and the
 * PCH documentation calls it the "PCI-LPC bridge" (when not calling it
 * something else).  See section 5.1, chapter 8 and chapter 10 of the PCH docs
 * for more Fun Facts!
 */
#define LPC_BRIDGE_BUS  (0x00)
#define LPC_BRIDGE_DEV  (0x1F)
#define LPC_BRIDGE_FUNC (0x00)
#define LPC_BRIDGE_VID  (0x8086)
static const uint16_t LPC_BRIDGE_DIDS[] = {
    0x9CC1,  // Full Featured Engineering Sample with Haswell U Processor
    0x9CC2,  // Full Featured Engineering Sample with Broadwell U Processor
    0x9CC3,  // Premium SKU with Broadwell U Processor
    0x9CC5,  // Base SKU with Broadwell U Processor
    0x9CC6,  // Full Featured Engineering Sample with Broadwell Y Processor
    0x9CC7,  // Premium SKU with Broadwell Y Processor
    0x9CC9,  // Base SKU with Broadwell Y Processor
};

/* IOBP registers.  Limited documentation available in section 8.1.24 */
#define IOBPIRI_OFFSET          (0x2330u)
#define IOBPD_OFFSET            (0x2334u)
#define IOBPS_OFFSET            (0x2338u)

#define IOBP_REGISTER_TIMEOUT   (100u)  // 100us.  Arbitrary; docs provides no guidance here.

#define IOBPS_BUSY_MASK         (0x00000001u)
#define IOBPS_BUSY_SHIFT        (0u)
#define IOBPS_BUSY(s)           (((s) & IOBPS_BUSY_MASK) >> IOBPS_BUSY_SHIFT)

#define IOBPS_STATUS_MASK       (0x00000006u)
#define IOBPS_STATUS_SHIFT      (1u)
#define IOBPS_STATUS(s)         (((s) & IOBPS_STATUS_MASK) >> IOBPS_STATUS_SHIFT)
#define IOBPS_STATUS_SUCCESS    (0u)
#define IOBPS_STATUS_NOIMPL     (1u)
#define IOBPS_STATUS_PWR_DOWN   (2u)

#define IOBPS_IFC_ACCESS_MASK   (0x0000FF00u)
#define IOBPS_IFC_ACCESS_SHIFT  (8u)
#define IOBPS_IFC_ACCESS_RD     (0x00u)
#define IOBPS_IFC_ACCESS_WR     (0x01u)
#define IOBPS_IFC_ACCESS_MMAP   (0x00u)
#define IOBPS_IFC_ACCESS_IOMAP  (0x02u)
#define IOBPS_IFC_ACCESS_PCICFG (0x04u)
#define IOBPS_IFC_ACCESS_ECTRL  (0x06u)
#define IOBPS_IFC_ACCESS_CMD(_zone, _rdwr)  \
    (0xF0000000 | \
     IOBPS_BUSY_MASK | \
     (((_zone | _rdwr) << IOBPS_IFC_ACCESS_SHIFT) & IOBPS_IFC_ACCESS_MASK))

/* Function Disable Registers, see 8.1.81 and 8.1.85 */
#define CCFG_FD_OFFSET          (0x3418u)
#define CCFG_FD2_OFFSET         (0x3428u)

typedef struct chipset_config_state {
    pcie_device_state_t* pci_device;

    vmm_aspace_t*        aspace;
    paddr_t              rcba_phys;
    void*                rcba_virt;

    // Cached pointers to the IOBP interface
    volatile uint32_t* IOBPIRI;
    volatile uint32_t* IOBPD;
    volatile uint32_t* IOBPS;
} chipset_config_state_t;

chipset_config_state_t g_chipset_config_state;
mutex_t                g_lock = MUTEX_INITIAL_VALUE(g_lock);

static inline status_t iobp_map_status(uint32_t status) {
    switch (IOBPS_STATUS(status)) {
        case IOBPS_STATUS_SUCCESS:  return NO_ERROR;
        case IOBPS_STATUS_NOIMPL:   return ERR_NOT_IMPLEMENTED;
        case IOBPS_STATUS_PWR_DOWN: return ERR_BAD_STATE;
        default:                    return ERR_GENERIC;
    }
}

static inline uint32_t wait_iobp_not_busy(chipset_config_state_t* state) {
    uint32_t status;
    lk_bigtime_t start = current_time_hires();

    while ((status = *state->IOBPS) & IOBPS_BUSY_MASK)
        if ((current_time_hires() - start) > IOBP_REGISTER_TIMEOUT)
            break;

    return status;
}

static inline status_t read_write_iobp(chipset_config_state_t* state,
                                       uint32_t index, uint32_t *data, bool write) {
    uint32_t status;

    DEBUG_ASSERT(state);
    DEBUG_ASSERT(data);

    status = wait_iobp_not_busy(state);
    if (IOBPS_BUSY(status))
        return ERR_TIMED_OUT;

    *state->IOBPIRI = index;
    if (write) {
        *state->IOBPD = *data;
        *state->IOBPS = IOBPS_IFC_ACCESS_CMD(IOBPS_IFC_ACCESS_ECTRL, IOBPS_IFC_ACCESS_WR);
    } else {
        *state->IOBPS = IOBPS_IFC_ACCESS_CMD(IOBPS_IFC_ACCESS_ECTRL, IOBPS_IFC_ACCESS_RD);
    }

    status = wait_iobp_not_busy(state);
    if (IOBPS_BUSY(status))
        return ERR_TIMED_OUT;

    if (!write)
        *data = *state->IOBPD;

    return iobp_map_status(status);
}

static status_t read_iobp(chipset_config_state_t* state, uint32_t index, uint32_t *out_data) {
    return read_write_iobp(state, index, out_data, false);
}

static uint32_t write_iobp(chipset_config_state_t* state, uint32_t index, uint32_t value) {
    return read_write_iobp(state, index, &value, true);
}

/****************************************************************************************
 *
 * PCI callback API
 *
 ****************************************************************************************/
static void* bcc_pci_probe(pcie_device_state_t* pci_device) {
    DEBUG_ASSERT(pci_device);
    chipset_config_state_t* state = &g_chipset_config_state;
    void* ret = NULL;

    mutex_acquire(&g_lock);

    if ((pci_device->vendor_id != LPC_BRIDGE_VID) ||
        (pci_device->bus_id    != LPC_BRIDGE_BUS) ||
        (pci_device->dev_id    != LPC_BRIDGE_DEV) ||
        (pci_device->func_id   != LPC_BRIDGE_FUNC))
        goto finished;

    size_t i;
    for (i = 0; i < countof(LPC_BRIDGE_DIDS); ++i)
        if (pci_device->device_id == LPC_BRIDGE_DIDS[i])
            break;

    if (i >= countof(LPC_BRIDGE_DIDS))
        goto finished;

    DEBUG_ASSERT(!state->pci_device);
    state->pci_device = pci_device;
    ret = state;

finished:
    mutex_release(&g_lock);
    return ret;
}

static void bcc_pci_shutdown_locked(pcie_device_state_t* pci_device) {
    chipset_config_state_t* state = (chipset_config_state_t*)pci_device->driver_ctx;
    DEBUG_ASSERT(state == &g_chipset_config_state);

    if (state->aspace) {
        if (state->rcba_virt) {
            vmm_free_region(state->aspace, (vaddr_t)state->rcba_virt);
            state->rcba_virt = NULL;
        }

        state->aspace = NULL;
    } else {
        DEBUG_ASSERT(!state->rcba_virt);
    }
}

static void bcc_pci_shutdown(pcie_device_state_t* pci_device) {
    mutex_acquire(&g_lock);
    bcc_pci_shutdown_locked(pci_device);
    mutex_release(&g_lock);
}

static status_t bcc_pci_startup(pcie_device_state_t* pci_device) {
    chipset_config_state_t* state = (chipset_config_state_t*)pci_device->driver_ctx;
    DEBUG_ASSERT(state == &g_chipset_config_state);

    status_t ret;
    mutex_acquire(&g_lock);

    /* Find the "root complex base address" and make sure the registers are enabled */
    state->rcba_phys = pcie_read32((volatile uint32_t*)((intptr_t)pci_device->cfg + 0xf0));
    state->rcba_phys |= 0x1;
    pcie_write32((volatile uint32_t*)((intptr_t)pci_device->cfg + 0xf0), state->rcba_phys);
    state->rcba_phys &= ~((1u << 14) - 1);

    /* Map in the chipset configuration registers */
    state->aspace = vmm_get_kernel_aspace();
    if (!state->aspace) {
        TRACEF("Failed to fetch kernel address space while attempting to map "
               "chipset configuration registers.\n");
        ret = ERR_BAD_STATE;
        goto finished;
    }

    ret = vmm_alloc_physical(state->aspace,
                             "BW_ChipsetConfigRegs",
                             0x4000,
                             &state->rcba_virt,
                             PAGE_SIZE_SHIFT,
                             state->rcba_phys,
                             0,
                             ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_NO_EXECUTE);

    if (ret != NO_ERROR) {
        TRACEF("Failed to map register window (0x%x @ 0x%llx) Status = %d\n",
                0x4000, (uint64_t)state->rcba_phys, ret);
        goto finished;
    }

    /* Stash pointers to the IOBP register block */
    state->IOBPIRI = (volatile uint32_t*)((intptr_t)state->rcba_virt + IOBPIRI_OFFSET);
    state->IOBPD   = (volatile uint32_t*)((intptr_t)state->rcba_virt + IOBPD_OFFSET);
    state->IOBPS   = (volatile uint32_t*)((intptr_t)state->rcba_virt + IOBPS_OFFSET);

finished:
    if (ret != NO_ERROR)
        bcc_pci_shutdown_locked(pci_device);

    mutex_release(&g_lock);
    return ret;
}

static void bcc_pci_release(pcie_device_state_t* pci_device) {
    mutex_acquire(&g_lock);

    chipset_config_state_t* state = (chipset_config_state_t*)pci_device->driver_ctx;
    DEBUG_ASSERT(state == &g_chipset_config_state);
    DEBUG_ASSERT(state->pci_device);
    DEBUG_ASSERT(!state->aspace);
    DEBUG_ASSERT(!state->rcba_phys);
    DEBUG_ASSERT(!state->rcba_virt);

    memset(state, 0, sizeof(*state));

    mutex_release(&g_lock);
}

static const pcie_driver_fn_table_t BCC_FN_TABLE = {
    .pcie_probe_fn    = bcc_pci_probe,
    .pcie_startup_fn  = bcc_pci_startup,
    .pcie_shutdown_fn = bcc_pci_shutdown,
    .pcie_release_fn  = bcc_pci_release,
};

STATIC_PCIE_DRIVER(intel_hda, "Broadwell Chipset Config", BCC_FN_TABLE)

/****************************************************************************************
 *
 * Target Facing API
 *
 ****************************************************************************************/
status_t bwcc_hide_device(bwcc_device_id_t which, bool hide) {
    chipset_config_state_t* state = &g_chipset_config_state;
    status_t ret;
    uint32_t reg;
    uint32_t clr_bits = 0x3 << 20;
    uint32_t set_bits = hide ? (1 << 20) : 0;

    mutex_acquire(&g_lock);

    if (!state->rcba_virt) {
        ret = ERR_NOT_READY;
        goto finished;
    }

    /* Appologies for all of the magic numbers here.  If this were not a dirty,
     * filty hack, I would clean this up.  Hopefully, however, we are not going
     * to be using this code for very long.
     *
     * Most of the addresses and bit patterns are documented in section
     * 8.1.24.11.x.   The SST audio DSP registers, however, are undocumented.
     * The magic numbers were found by working backwards from the coreboot code.
     * */
    switch (which) {
        case BWCC_DEV_SERIAL_DMA_IO: reg = 0xcb000240; break;
        case BWCC_DEV_I2C0:          reg = 0xcb000248; break;
        case BWCC_DEV_I2C1:          reg = 0xcb000250; break;
        case BWCC_DEV_SPI0:          reg = 0xcb000258; break;
        case BWCC_DEV_SPI1:          reg = 0xcb000260; break;
        case BWCC_DEV_UART0:         reg = 0xcb000268; break;
        case BWCC_DEV_UART1:         reg = 0xcb000270; break;

        case BWCC_DEV_SST:
            reg      = 0xd7000500;
            clr_bits = 0x00000083;
            set_bits = hide ? 0x00000001 : 0;
            break;

        case BWCC_DEV_SDIO:
            ret = ERR_NOT_IMPLEMENTED;
            goto finished;

        default:
            ret = ERR_INVALID_ARGS;
            goto finished;
    }

    uint32_t val;
    ret = read_iobp(state, reg, &val);
    if (ret != NO_ERROR)
        goto finished;

    val = (val & ~clr_bits) | set_bits;
    ret = write_iobp(state, reg, val);

finished:
    mutex_release(&g_lock);
    return ret;
}

status_t bwcc_disable_fd_fd2_device(chipset_config_state_t* state,
                                    uint32_t offset,
                                    uint32_t bit,
                                    bool disable) {
    /* According to docs, for devices controlled by the FD/FD2 registers in the
     * core Chipset Control block, it is OK to disable a device, but never to
     * re-enable the device once it has been disabled.  So, if someone is asking
     * to enable a device, we succeed if it is already enabled and fail
     * otherwise.
     */
    volatile uint32_t* reg = (volatile uint32_t*)((intptr_t)state->rcba_virt + offset);

    if (!disable)
        return *reg & bit ? ERR_NOT_SUPPORTED : NO_ERROR;

    *reg = *reg | bit;
    return NO_ERROR;
}

status_t bwcc_disable_device(bwcc_device_id_t which, bool disable) {
    chipset_config_state_t* state = &g_chipset_config_state;
    status_t ret;
    uint32_t reg;

    mutex_acquire(&g_lock);

    if (!state->rcba_virt) {
        ret = ERR_NOT_READY;
        goto finished;
    }

    /* Appologies for all of the magic numbers here.  If this were not a dirty,
     * filty hack, I would clean this up.  Hopefully, however, we are not going
     * to be using this code for very long.  */
    switch (which) {
        case BWCC_DEV_SERIAL_DMA_IO: reg = 0xce00aa07; break;
        case BWCC_DEV_I2C0:          reg = 0xce00aa47; break;
        case BWCC_DEV_I2C1:          reg = 0xce00aa87; break;
        case BWCC_DEV_SPI0:          reg = 0xce00aac7; break;
        case BWCC_DEV_SPI1:          reg = 0xce00ab07; break;
        case BWCC_DEV_UART0:         reg = 0xce00ab47; break;
        case BWCC_DEV_UART1:         reg = 0xce00ab87; break;
        case BWCC_DEV_SDIO:          reg = 0xce00ae07; break;

        /* Control of enable/disable for the SST DSP is in the Chipset Config FD
         * register (see section 8.1.81). */
        case BWCC_DEV_SST:
            ret = bwcc_disable_fd_fd2_device(state, CCFG_FD_OFFSET, (1 << 1), disable);
            goto finished;

        default:
            ret = ERR_INVALID_ARGS;
            goto finished;
    }

    uint32_t val;
    ret = read_iobp(state, reg, &val);
    if (ret != NO_ERROR)
        goto finished;

    val = (val & ~(0x1u << 8)) | ((disable ? 1 : 0) << 8);
    ret = write_iobp(state, reg, val);

finished:
    mutex_release(&g_lock);
    return ret;
}
