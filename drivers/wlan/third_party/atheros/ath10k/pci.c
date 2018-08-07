/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "pci.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <wlan/protocol/mac.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "bmi.h"
#include "ce.h"
#include "core.h"
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "ieee80211.h"
#include "mac.h"
#include "macros.h"
#include "targaddrs.h"

enum ath10k_pci_reset_mode {
    ATH10K_PCI_RESET_AUTO = 0,
    ATH10K_PCI_RESET_WARM_ONLY = 1,
};

// Linux module paramters
static unsigned int ath10k_pci_irq_mode = ATH10K_PCI_IRQ_AUTO;
static unsigned int ath10k_pci_reset_mode = ATH10K_PCI_RESET_AUTO;

/* how long wait to wait for target to initialise, in ms */
#define ATH10K_PCI_TARGET_WAIT 3000
#define ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS 3

static const struct ath10k_pci_supp_chip ath10k_pci_supp_chips[] = {
    /* QCA988X pre 2.0 chips are not supported because they need some nasty
     * hacks. ath10k doesn't have them and these devices crash horribly
     * because of that.
     */
    {QCA988X_2_0_DEVICE_ID, QCA988X_HW_2_0_CHIP_ID_REV},

    {QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV},
    {QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV},
    {QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV},
    {QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV},
    {QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV},

    {QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV},
    {QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV},
    {QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV},
    {QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV},
    {QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV},

    {QCA99X0_2_0_DEVICE_ID, QCA99X0_HW_2_0_CHIP_ID_REV},

    {QCA9984_1_0_DEVICE_ID, QCA9984_HW_1_0_CHIP_ID_REV},

    {QCA9888_2_0_DEVICE_ID, QCA9888_HW_2_0_CHIP_ID_REV},

    {QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_0_CHIP_ID_REV},
    {QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_1_CHIP_ID_REV},

    {QCA9887_1_0_DEVICE_ID, QCA9887_HW_1_0_CHIP_ID_REV},
};

static void ath10k_pci_buffer_cleanup(struct ath10k* ar);
static zx_status_t ath10k_pci_cold_reset(struct ath10k* ar);
static zx_status_t ath10k_pci_safe_chip_reset(struct ath10k* ar);
static zx_status_t ath10k_pci_init_irq(struct ath10k* ar);
static void ath10k_pci_deinit_irq(struct ath10k* ar);
static zx_status_t ath10k_pci_request_irq(struct ath10k* ar);
static void ath10k_pci_free_irq(struct ath10k* ar);
static zx_status_t ath10k_pci_bmi_wait(struct ath10k* ar, struct ath10k_ce_pipe* tx_pipe,
                                       struct ath10k_ce_pipe* rx_pipe, struct bmi_xfer* xfer);
static zx_status_t ath10k_pci_qca99x0_chip_reset(struct ath10k* ar);
static void ath10k_pci_htc_tx_cb(struct ath10k_ce_pipe* ce_state);
static void ath10k_pci_htc_rx_cb(struct ath10k_ce_pipe* ce_state);
static void ath10k_pci_htt_tx_cb(struct ath10k_ce_pipe* ce_state);
static void ath10k_pci_htt_rx_cb(struct ath10k_ce_pipe* ce_state);
static void ath10k_pci_htt_htc_rx_cb(struct ath10k_ce_pipe* ce_state);
static void ath10k_pci_pktlog_rx_cb(struct ath10k_ce_pipe* ce_state);

static struct ce_attr host_ce_config_wlan[] = {
    /* CE0: host->target HTC control and raw streams */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 16,
        .src_sz_max = 256,
        .dest_nentries = 0,
        .send_cb = ath10k_pci_htc_tx_cb,
    },

    /* CE1: target->host HTT + HTC control */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 2048,
        .dest_nentries = 512,
        .recv_cb = ath10k_pci_htt_htc_rx_cb,
    },

    /* CE2: target->host WMI */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 2048,
        .dest_nentries = 128,
        .recv_cb = ath10k_pci_htc_rx_cb,
    },

    /* CE3: host->target WMI */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 32,
        .src_sz_max = 2048,
        .dest_nentries = 0,
        .send_cb = ath10k_pci_htc_tx_cb,
    },

    /* CE4: host->target HTT */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = CE_HTT_H2T_MSG_SRC_NENTRIES,
        .src_sz_max = 256,
        .dest_nentries = 0,
        .send_cb = ath10k_pci_htt_tx_cb,
    },

    /* CE5: target->host HTT (HIF->HTT) */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 512,
        .dest_nentries = 512,
        .recv_cb = ath10k_pci_htt_rx_cb,
    },

    /* CE6: target autonomous hif_memcpy */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 0,
        .dest_nentries = 0,
    },

    /* CE7: ce_diag, the Diagnostic Window */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 2,
        .src_sz_max = DIAG_TRANSFER_LIMIT,
        .dest_nentries = 2,
    },

    /* CE8: target->host pktlog */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 2048,
        .dest_nentries = 128,
        .recv_cb = ath10k_pci_pktlog_rx_cb,
    },

    /* CE9 target autonomous qcache memcpy */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 0,
        .dest_nentries = 0,
    },

    /* CE10: target autonomous hif memcpy */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 0,
        .dest_nentries = 0,
    },

    /* CE11: target autonomous hif memcpy */
    {
        .flags = CE_ATTR_FLAGS,
        .src_nentries = 0,
        .src_sz_max = 0,
        .dest_nentries = 0,
    },
};

/* Target firmware's Copy Engine configuration. */
static struct ce_pipe_config target_ce_config_wlan[] = {
    /* CE0: host->target HTC control and raw streams */
    {
        .pipenum = 0,
        .pipedir = PIPEDIR_OUT,
        .nentries = 32,
        .nbytes_max = 256,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE1: target->host HTT + HTC control */
    {
        .pipenum = 1,
        .pipedir = PIPEDIR_IN,
        .nentries = 32,
        .nbytes_max = 2048,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE2: target->host WMI */
    {
        .pipenum = 2,
        .pipedir = PIPEDIR_IN,
        .nentries = 64,
        .nbytes_max = 2048,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE3: host->target WMI */
    {
        .pipenum = 3,
        .pipedir = PIPEDIR_OUT,
        .nentries = 32,
        .nbytes_max = 2048,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE4: host->target HTT */
    {
        .pipenum = 4,
        .pipedir = PIPEDIR_OUT,
        .nentries = 256,
        .nbytes_max = 256,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* NB: 50% of src nentries, since tx has 2 frags */

    /* CE5: target->host HTT (HIF->HTT) */
    {
        .pipenum = 5,
        .pipedir = PIPEDIR_IN,
        .nentries = 32,
        .nbytes_max = 512,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE6: Reserved for target autonomous hif_memcpy */
    {
        .pipenum = 6,
        .pipedir = PIPEDIR_INOUT,
        .nentries = 32,
        .nbytes_max = 4096,
        .flags = CE_ATTR_FLAGS,
        .reserved = 0,
    },

    /* CE7 used only by Host */
    {
        .pipenum = 7,
        .pipedir = PIPEDIR_INOUT,
        .nentries = 0,
        .nbytes_max = 0,
        .flags = 0,
        .reserved = 0,
    },

    /* CE8 target->host packtlog */
    {
        .pipenum = 8,
        .pipedir = PIPEDIR_IN,
        .nentries = 64,
        .nbytes_max = 2048,
        .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
        .reserved = 0,
    },

    /* CE9 target autonomous qcache memcpy */
    {
        .pipenum = 9,
        .pipedir = PIPEDIR_INOUT,
        .nentries = 32,
        .nbytes_max = 2048,
        .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
        .reserved = 0,
    },

    /* It not necessary to send target wlan configuration for CE10 & CE11
     * as these CEs are not actively used in target.
     */
};

/*
 * Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static struct service_to_pipe target_service_to_ce_map_wlan[] = {
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VO,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VO,
        PIPEDIR_IN, /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BK,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BK,
        PIPEDIR_IN, /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BE,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BE,
        PIPEDIR_IN, /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VI,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VI,
        PIPEDIR_IN, /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_CONTROL,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_CONTROL,
        PIPEDIR_IN, /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_RSVD_CTRL,
        PIPEDIR_OUT, /* out = UL = host -> target */
        0,
    },
    {
        ATH10K_HTC_SVC_ID_RSVD_CTRL,
        PIPEDIR_IN, /* in = DL = target -> host */
        1,
    },
    {
        /* not used */
        ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,
        PIPEDIR_OUT, /* out = UL = host -> target */
        0,
    },
    {
        /* not used */
        ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,
        PIPEDIR_IN, /* in = DL = target -> host */
        1,
    },
    {
        ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
        PIPEDIR_OUT, /* out = UL = host -> target */
        4,
    },
    {
        ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
        PIPEDIR_IN, /* in = DL = target -> host */
        5,
    },

    /* (Additions here) */

    {
        /* must be last */
        0,
        0,
        0,
    },
};

static bool ath10k_pci_is_awake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    uint32_t val = READ32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + RTC_STATE_ADDRESS);

    return RTC_STATE_V_GET(val) == RTC_STATE_V_ON;
}

static zx_status_t __ath10k_pci_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ASSERT_MTX_HELD(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps wake reg refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS, PCIE_SOC_WAKE_V_MASK);
    return ZX_OK;
}

#if 0   // NEEDS PORTING
static void __ath10k_pci_sleep(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ASSERT_MTX_HELD(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps sleep reg refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
            PCIE_SOC_WAKE_RESET);
    ar_pci->ps_awake = false;
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_pci_wake_wait(struct ath10k* ar) {
    int tot_delay = 0;
    int curr_delay = 5;

    while (tot_delay < PCIE_WAKE_TIMEOUT) {
        if (ath10k_pci_is_awake(ar)) {
            if (tot_delay > PCIE_WAKE_LATE_US)
                ath10k_warn(
                    "device wakeup took %d ms which is unusually long, otherwise it works "
                    "normally.\n",
                    tot_delay / 1000);
            return ZX_OK;
        }

        zx_nanosleep(zx_deadline_after(ZX_USEC(curr_delay)));
        tot_delay += curr_delay;

        if (curr_delay < 50) { curr_delay += 5; }
    }

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t ath10k_pci_force_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;

    if (ar_pci->pci_ps) { return ret; }

    mtx_lock(&ar_pci->ps_lock);

    if (!ar_pci->ps_awake) {
        WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
                PCIE_SOC_WAKE_V_MASK);

        ret = ath10k_pci_wake_wait(ar);
        if (ret == ZX_OK) { ar_pci->ps_awake = true; }
    }

    mtx_unlock(&ar_pci->ps_lock);

    return ret;
}

#if 0   // NEEDS PORTING
static void ath10k_pci_force_sleep(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    mtx_lock(&ar_pci->ps_lock);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
            PCIE_SOC_WAKE_RESET);
    ar_pci->ps_awake = false;

    mtx_unlock(&ar_pci->ps_lock);
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_pci_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;

    if (ar_pci->pci_ps == 0) { return ret; }

    mtx_lock(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps wake refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    /* This function can be called very frequently. To avoid excessive
     * CPU stalls for MMIO reads use a cache var to hold the device state.
     */
    if (!ar_pci->ps_awake) {
        __ath10k_pci_wake(ar);

        ret = ath10k_pci_wake_wait(ar);
        if (ret == ZX_OK) { ar_pci->ps_awake = true; }
    }

    if (ret == ZX_OK) {
        ar_pci->ps_wake_refcount++;
        COND_WARN(ar_pci->ps_wake_refcount == 0);
    }

    mtx_unlock(&ar_pci->ps_lock);

    return ret;
}

static void ath10k_pci_sleep(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (ar_pci->pci_ps == 0) { return; }

    mtx_lock(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps sleep refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    if (COND_WARN(ar_pci->ps_wake_refcount == 0)) { goto skip; }

    ar_pci->ps_wake_refcount--;

#if 0   // NEEDS PORTING
    mod_timer(&ar_pci->ps_timer, jiffies +
              msecs_to_jiffies(ATH10K_PCI_SLEEP_GRACE_PERIOD_MSEC));
#endif  // NEEDS PORTING

skip:
    mtx_unlock(&ar_pci->ps_lock);
}

#if 0   // NEEDS PORTING
static void ath10k_pci_ps_timer(unsigned long ptr) {
    struct ath10k* ar = (void*)ptr;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    mtx_lock(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps timer refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    if (ar_pci->ps_wake_refcount > 0) {
        goto skip;
    }

    __ath10k_pci_sleep(ar);

skip:
    mtx_unlock(&ar_pci->ps_lock);
}

static void ath10k_pci_sleep_sync(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (ar_pci->pci_ps == 0) {
        ath10k_pci_force_sleep(ar);
        return;
    }

    del_timer_sync(&ar_pci->ps_timer);

    mtx_lock(&ar_pci->ps_lock);
    COND_WARN(ar_pci->ps_wake_refcount > 0);
    __ath10k_pci_sleep(ar);
    mtx_unlock(&ar_pci->ps_lock);
}
#endif  // NEEDS PORTING

static void ath10k_bus_pci_write32(struct ath10k* ar, uint32_t offset, uint32_t value) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    if (unlikely(offset + sizeof(value) > ar_pci->mem_len)) {
        ath10k_warn("refusing to write mmio out of bounds at 0x%08x - 0x%08zx (max 0x%08zx)\n",
                    offset, offset + sizeof(value), ar_pci->mem_len);
        return;
    }

    ret = ath10k_pci_wake(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wake target for write32 of 0x%08x at 0x%08x: %s\n", value, offset,
                    zx_status_get_string(ret));
        return;
    }

    WRITE32(ar_pci->mem + offset, value);
    ath10k_pci_sleep(ar);
}

static uint32_t ath10k_bus_pci_read32(struct ath10k* ar, uint32_t offset) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    uint32_t val;
    zx_status_t ret;

    if (unlikely(offset + sizeof(val) > ar_pci->mem_len)) {
        ath10k_warn("refusing to read mmio out of bounds at 0x%08x - 0x%08zx (max 0x%08zx)\n",
                    offset, offset + sizeof(val), ar_pci->mem_len);
        return 0;
    }

    ret = ath10k_pci_wake(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wake target for read32 at 0x%08x: %s\n", offset,
                    zx_status_get_string(ret));
        return 0xffffffff;
    }

    val = READ32(ar_pci->mem + offset);
    ath10k_pci_sleep(ar);

    return val;
}

inline void ath10k_pci_write32(struct ath10k* ar, uint32_t offset, uint32_t value) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ar_pci->bus_ops->write32(ar, offset, value);
}

inline uint32_t ath10k_pci_read32(struct ath10k* ar, uint32_t offset) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    return ar_pci->bus_ops->read32(ar, offset);
}

uint32_t ath10k_pci_soc_read32(struct ath10k* ar, uint32_t addr) {
    return ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + addr);
}

void ath10k_pci_soc_write32(struct ath10k* ar, uint32_t addr, uint32_t val) {
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + addr, val);
}

uint32_t ath10k_pci_reg_read32(struct ath10k* ar, uint32_t addr) {
    return ath10k_pci_read32(ar, PCIE_LOCAL_BASE_ADDRESS + addr);
}

void ath10k_pci_reg_write32(struct ath10k* ar, uint32_t addr, uint32_t val) {
    ath10k_pci_write32(ar, PCIE_LOCAL_BASE_ADDRESS + addr, val);
}

bool ath10k_pci_irq_pending(struct ath10k* ar) {
    uint32_t cause;

    /* Check if the shared legacy irq is for us */
    cause = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_CAUSE_ADDRESS);
    if (cause & (PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL)) { return true; }

    return false;
}

void ath10k_pci_disable_and_clear_legacy_irq(struct ath10k* ar) {
    /* IMPORTANT: INTR_CLR register has to be set after
     * INTR_ENABLE is set to 0, otherwise interrupt can not be
     * really cleared.
     */
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS, 0);
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_CLR_ADDRESS,
                       PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

    /* IMPORTANT: this extra read transaction is required to
     * flush the posted write buffer.
     */
    (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS);
}

void ath10k_pci_enable_legacy_irq(struct ath10k* ar) {
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                       PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

    /* IMPORTANT: this extra read transaction is required to
     * flush the posted write buffer.
     */
    (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS);
}

static inline const char* ath10k_pci_get_irq_method(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_MSI) { return "msi"; }

    return "legacy";
}

static zx_status_t __ath10k_pci_rx_post_buf(struct ath10k_pci_pipe* pipe) {
    struct ath10k* ar = pipe->hif_ce_state;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_ce_pipe* ce_pipe = pipe->ce_hdl;
    struct ath10k_msg_buf* buf;
    zx_status_t ret;

    ret = ath10k_msg_buf_alloc(ar, &buf, ATH10K_MSG_TYPE_BASE, pipe->buf_sz);
    if (ret != ZX_OK) { return ret; }

    if ((uintptr_t)buf->vaddr & 3) {
        ath10k_err("misaligned rx buffer\n");
        ret = ZX_ERR_INTERNAL;
        goto err;
    }

    mtx_lock(&ar_pci->ce_lock);
    ret = __ath10k_ce_rx_post_buf(ce_pipe, buf, buf->paddr);
    mtx_unlock(&ar_pci->ce_lock);
    if (ret != ZX_OK) { goto err; }
    return ZX_OK;

err:
    ath10k_msg_buf_free(buf);
    return ret;
}

static void ath10k_pci_rx_post_pipe(struct ath10k_pci_pipe* pipe) {
    struct ath10k* ar = pipe->hif_ce_state;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_ce_pipe* ce_pipe = pipe->ce_hdl;
    zx_status_t ret;
    int num;

    if (pipe->buf_sz == 0) { return; }

    if (!ce_pipe->dest_ring) { return; }

    mtx_lock(&ar_pci->ce_lock);
    num = __ath10k_ce_rx_num_free_bufs(ce_pipe);
    mtx_unlock(&ar_pci->ce_lock);

    while (num >= 0) {
        ret = __ath10k_pci_rx_post_buf(pipe);
        if (ret != ZX_OK) {
            ZX_DEBUG_ASSERT(ret == ZX_ERR_NO_SPACE);
            break;
        }
        num--;
    }
}

void ath10k_pci_rx_post(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    int i;

    for (i = 0; i < CE_COUNT; i++) {
        ath10k_pci_rx_post_pipe(&ar_pci->pipe_info[i]);
    }
}

static zx_status_t ath10k_pci_qca988x_targ_cpu_to_ce_addr(struct ath10k* ar, uint32_t addr,
                                                          uint32_t* ce_addr) {
    uint32_t val = 0, region = addr & 0xfffff;

    val = (ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS) & 0x7ff) << 21;
    val |= 0x100000 | region;
    *ce_addr = val;
    return ZX_OK;
}

static zx_status_t ath10k_pci_qca99x0_targ_cpu_to_ce_addr(struct ath10k* ar, uint32_t addr,
                                                          uint32_t* ce_addr) {
    uint32_t val = 0, region = addr & 0xfffff;

    val = ath10k_pci_read32(ar, PCIE_BAR_REG_ADDRESS);
    val |= 0x100000 | region;
    *ce_addr = val;
    return ZX_OK;
}

static zx_status_t ath10k_pci_targ_cpu_to_ce_addr(struct ath10k* ar, uint32_t addr,
                                                  uint32_t* ce_addr) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (COND_WARN_ONCE(!ar_pci->targ_cpu_to_ce_addr)) { return ZX_ERR_NOT_SUPPORTED; }

    return ar_pci->targ_cpu_to_ce_addr(ar, addr, ce_addr);
}

/*
 * Diagnostic read/write access is provided for startup/config/debug usage.
 * Caller must guarantee proper alignment, when applicable, and single user
 * at any moment.
 */
static zx_status_t ath10k_pci_diag_read_mem(struct ath10k* ar, uint32_t address, void* data,
                                            int nbytes) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;
    uint32_t* buf;
    unsigned int completed_nbytes, alloc_nbytes, remaining_bytes;
    struct ath10k_ce_pipe* ce_diag;
    /* Host buffer address in CE space */
    uint32_t ce_data;
    void* data_buf = NULL;
    int i;

    mtx_lock(&ar_pci->ce_lock);

    ce_diag = ar_pci->ce_diag;

    /*
     * Allocate a temporary buffer to hold caller's data
     * to be DMA'ed from Target.
     */
    alloc_nbytes = MIN_T(unsigned int, nbytes, DIAG_TRANSFER_LIMIT);

    struct ath10k_msg_buf* iobuf;
    ret = ath10k_msg_buf_alloc(ar, &iobuf, ATH10K_MSG_TYPE_BASE, alloc_nbytes);
    if (ret != ZX_OK) { goto done; }
    data_buf = iobuf->vaddr;
    // ath10k_msg_buf_alloc verifies that the address will fit into 32 bits
    ce_data = iobuf->paddr;
    remaining_bytes = nbytes;
    while (remaining_bytes) {
        nbytes = MIN_T(unsigned int, remaining_bytes, DIAG_TRANSFER_LIMIT);

        ret = __ath10k_ce_rx_post_buf(ce_diag, &ce_data, ce_data);
        if (ret != ZX_OK) { goto done; }

        /* Request CE to send from Target(!) address to Host buffer */
        /*
         * The address supplied by the caller is in the
         * Target CPU virtual address space.
         *
         * In order to use this address with the diagnostic CE,
         * convert it from Target CPU virtual address space
         * to CE address space
         */
        uint32_t ce_address;
        ret = ath10k_pci_targ_cpu_to_ce_addr(ar, address, &ce_address);
        if (ret != ZX_OK) { goto done; }

        ret = ath10k_ce_send_nolock(ce_diag, NULL, ce_address, nbytes, 0, 0);
        if (ret != ZX_OK) { goto done; }

        i = 0;
        while (ath10k_ce_completed_send_next_nolock(ce_diag, NULL) != ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        i = 0;
        while (ath10k_ce_completed_recv_next_nolock(ce_diag, (void**)&buf, &completed_nbytes) !=
               ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        if ((unsigned int)nbytes != completed_nbytes) {
            ret = ZX_ERR_IO;
            goto done;
        }

        if (*buf != ce_data) {
            ret = ZX_ERR_IO;
            goto done;
        }

        remaining_bytes -= nbytes;
        memcpy(data, data_buf, nbytes);

        address += nbytes;
        data += nbytes;
    }

done:
    if (data_buf) { ath10k_msg_buf_free(iobuf); }

    mtx_unlock(&ar_pci->ce_lock);

    return ret;
}

static zx_status_t ath10k_pci_diag_read32(struct ath10k* ar, uint32_t address, uint32_t* value) {
    return ath10k_pci_diag_read_mem(ar, address, value, sizeof(*value));
}

#if 0  // NEEDS PORTING
static zx_status_t __ath10k_pci_diag_read_hi(struct ath10k* ar, void* dest,
                                             uint32_t src, uint32_t len) {
    uint32_t host_addr, addr;
    zx_status_t ret;

    host_addr = host_interest_item_address(src);

    ret = ath10k_pci_diag_read32(ar, host_addr, &addr);
    if (ret != ZX_OK) {
        ath10k_warn("failed to get memcpy hi address for firmware address %d: %s\n",
                    src, zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_diag_read_mem(ar, addr, dest, len);
    if (ret != ZX_OK) {
        ath10k_warn("failed to memcpy firmware memory from %d (%d B): %s\n",
                    addr, len, zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

#define ath10k_pci_diag_read_hi(ar, dest, src, len) \
    __ath10k_pci_diag_read_hi(ar, dest, HI_ITEM(src), len)
#endif  // NEEDS PORTING

zx_status_t ath10k_pci_diag_write_mem(struct ath10k* ar, uint32_t address, const void* data,
                                      int nbytes) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;
    uint32_t* buf;
    unsigned int completed_nbytes, orig_nbytes, remaining_bytes;
    struct ath10k_ce_pipe* ce_diag;
    void* data_buf = NULL;
    uint32_t ce_data; /* Host buffer address in CE space */
    zx_paddr_t ce_data_base = 0;
    struct ath10k_msg_buf* iobuf;
    int i;

    mtx_lock(&ar_pci->ce_lock);

    ce_diag = ar_pci->ce_diag;

    /*
     * Allocate a temporary buffer to hold caller's data
     * to be DMA'ed to Target.
     */
    orig_nbytes = nbytes;
    ret = ath10k_msg_buf_alloc(ar, &iobuf, ATH10K_MSG_TYPE_BASE, orig_nbytes);
    if (ret != ZX_OK) { goto done; }
    data_buf = iobuf->vaddr;
    ce_data_base = iobuf->paddr;

    /* Copy caller's data to allocated DMA buf */
    memcpy(data_buf, data, orig_nbytes);

    /*
     * The address supplied by the caller is in the
     * Target CPU virtual address space.
     *
     * In order to use this address with the diagnostic CE,
     * convert it from
     *    Target CPU virtual address space
     * to
     *    CE address space
     */
    uint32_t ce_address;
    ret = ath10k_pci_targ_cpu_to_ce_addr(ar, address, &ce_address);
    if (ret != ZX_OK) { goto done; }

    remaining_bytes = orig_nbytes;
    ce_data = ce_data_base;
    while (remaining_bytes) {
        /* FIXME: check cast */
        nbytes = MIN_T(int, remaining_bytes, DIAG_TRANSFER_LIMIT);

        /* Set up to receive directly into Target(!) address */
        ret = __ath10k_ce_rx_post_buf(ce_diag, &ce_address, ce_address);
        if (ret != ZX_OK) { goto done; }

        /*
         * Request CE to send caller-supplied data that
         * was copied to bounce buffer to Target(!) address.
         */
        ret = ath10k_ce_send_nolock(ce_diag, NULL, (uint32_t)ce_data, nbytes, 0, 0);
        if (ret != ZX_OK) { goto done; }

        i = 0;
        while (ath10k_ce_completed_send_next_nolock(ce_diag, NULL) != ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        i = 0;
        while (ath10k_ce_completed_recv_next_nolock(ce_diag, (void**)&buf, &completed_nbytes) !=
               ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        if ((unsigned int)nbytes != completed_nbytes) {
            ret = ZX_ERR_IO;
            goto done;
        }

        if (*buf != ce_address) {
            ret = ZX_ERR_IO;
            goto done;
        }

        remaining_bytes -= nbytes;
        ce_address += nbytes;
        ce_data += nbytes;
    }

done:
    if (data_buf) { ath10k_msg_buf_free(iobuf); }

    if (ret != ZX_OK) {
        ath10k_warn("failed to write diag value at 0x%x: %s\n", address, zx_status_get_string(ret));
    }

    mtx_unlock(&ar_pci->ce_lock);

    return ret;
}

static zx_status_t ath10k_pci_diag_write32(struct ath10k* ar, uint32_t address, uint32_t value) {
    uint32_t val = value;

    return ath10k_pci_diag_write_mem(ar, address, &val, sizeof(val));
}

static zx_status_t ath10k_pci_hif_get_bti_handle(struct ath10k* ar, zx_handle_t* bti_handle) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    *bti_handle = ar_pci->btih;
    return ZX_OK;
}

/* Called by lower (CE) layer when a send to Target completes. */
static void ath10k_pci_htc_tx_cb(struct ath10k_ce_pipe* ce_state) {
    struct ath10k* ar = ce_state->ar;
    list_node_t list;
    struct ath10k_msg_buf* msg_buf;

    list_initialize(&list);
    while (ath10k_ce_completed_send_next(ce_state, (void**)&msg_buf) == ZX_OK) {
        /* no need to call tx completion for NULL pointers */
        if (msg_buf == NULL) { continue; }

        list_add_tail(&list, &msg_buf->listnode);
    }

    while ((msg_buf = list_remove_head_type(&list, struct ath10k_msg_buf, listnode)) != NULL) {
        ath10k_htc_tx_completion_handler(ar, msg_buf);
    }
}

static void ath10k_pci_process_rx_cb(struct ath10k_ce_pipe* ce_state,
                                     void (*callback)(struct ath10k* ar,
                                                      struct ath10k_msg_buf* buf)) {
    struct ath10k* ar = ce_state->ar;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pipe_info = &ar_pci->pipe_info[ce_state->id];
    struct ath10k_msg_buf* buf;
    list_node_t list;
    void* transfer_context;
    unsigned int nbytes, max_nbytes;

    list_initialize(&list);
    while (ath10k_ce_completed_recv_next(ce_state, &transfer_context, &nbytes) == ZX_OK) {
        buf = transfer_context;
        max_nbytes = buf->capacity;

        if (unlikely(max_nbytes < nbytes)) {
            ath10k_warn("rxed more than expected (nbytes %d, max %d)", nbytes, max_nbytes);
            ath10k_msg_buf_free(buf);
            continue;
        }

        buf->used += nbytes;
        list_add_tail(&list, &buf->listnode);
    }

    while ((buf = list_remove_head_type(&list, struct ath10k_msg_buf, listnode)) != NULL) {
        callback(ar, buf);
    }

    ath10k_pci_rx_post_pipe(pipe_info);
}

static void ath10k_pci_process_htt_rx_cb(struct ath10k_ce_pipe* ce_state,
                                         void (*callback)(struct ath10k* ar,
                                                          struct ath10k_msg_buf* msg_buf)) {
    struct ath10k* ar = ce_state->ar;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pipe_info = &ar_pci->pipe_info[ce_state->id];
    struct ath10k_ce_pipe* ce_pipe = pipe_info->ce_hdl;
    struct ath10k_msg_buf* msg_buf;
    void* transfer_context;
    unsigned int nbytes, max_nbytes, nentries = 0;

    /* No need to aquire ce_lock for CE5, since this is the only place CE5
     * is processed other than init and deinit. Before releasing CE5
     * buffers, interrupts are disabled. Thus CE5 access is serialized.
     */
    while (ath10k_ce_completed_recv_next_nolock(ce_state, &transfer_context, &nbytes) == 0) {
        msg_buf = transfer_context;
        max_nbytes = msg_buf->capacity;

        if (unlikely(max_nbytes < nbytes)) {
            ath10k_warn("rxed more than expected (nbytes %d, max %d)", nbytes, max_nbytes);
            continue;
        }

        io_buffer_cache_flush_invalidate(&msg_buf->buf, 0, max_nbytes);

        msg_buf->used = nbytes;
        nentries++;

        ath10k_dbg(ar, ATH10K_DBG_PCI, "pci rx ce pipe %d len %d\n", ce_state->id, msg_buf->used);
        ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci rx: ", msg_buf->vaddr, msg_buf->used);

        callback(ar, msg_buf);

        /*let device gain the buffer again*/
        io_buffer_cache_flush(&msg_buf->buf, 0, max_nbytes);
    }
    ath10k_ce_rx_update_write_idx(ce_pipe, nentries);
}

/* Called by lower (CE) layer when data is received from the Target. */
static void ath10k_pci_htc_rx_cb(struct ath10k_ce_pipe* ce_state) {
    ath10k_pci_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
}

static void ath10k_pci_htt_htc_rx_cb(struct ath10k_ce_pipe* ce_state) {
    /* CE4 polling needs to be done whenever CE pipe which transports
     * HTT Rx (target->host) is processed.
     */
    ath10k_ce_per_engine_service(ce_state->ar, 4);

    ath10k_pci_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
}

/* Called by lower (CE) layer when data is received from the Target.
 * Only 10.4 firmware uses separate CE to transfer pktlog data.
 */
static void ath10k_pci_pktlog_rx_cb(struct ath10k_ce_pipe* ce_state) {
    ath10k_pci_process_rx_cb(ce_state, ath10k_htt_rx_pktlog_completion_handler);
}

/* Called by lower (CE) layer when a send to HTT Target completes. */
static void ath10k_pci_htt_tx_cb(struct ath10k_ce_pipe* ce_state) {
    struct ath10k* ar = ce_state->ar;
    struct ath10k_msg_buf* msg_buf;

    while (ath10k_ce_completed_send_next(ce_state, (void**)&msg_buf) == ZX_OK) {
        /* no need to call tx completion for NULL pointers */
        if (msg_buf == NULL) { continue; }

        ath10k_htt_hif_tx_complete(ar, msg_buf);
    }
}

static void ath10k_pci_htt_rx_deliver(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    ath10k_htt_t2h_msg_handler(ar, msg_buf);
}

/* Called by lower (CE) layer when HTT data is received from the Target. */
static void ath10k_pci_htt_rx_cb(struct ath10k_ce_pipe* ce_state) {
    /* CE4 polling needs to be done whenever CE pipe which transports
     * HTT Rx (target->host) is processed.
     */
    ath10k_ce_per_engine_service(ce_state->ar, 4);

    ath10k_pci_process_htt_rx_cb(ce_state, ath10k_pci_htt_rx_deliver);
}

zx_status_t ath10k_pci_hif_tx_sg(struct ath10k* ar, uint8_t pipe_id,
                                 struct ath10k_hif_sg_item* items, int n_items) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pci_pipe = &ar_pci->pipe_info[pipe_id];
    struct ath10k_ce_pipe* ce_pipe = pci_pipe->ce_hdl;
    struct ath10k_ce_ring* src_ring = ce_pipe->src_ring;
    unsigned int nentries_mask;
    unsigned int sw_index;
    unsigned int write_index;
    zx_status_t err;
    int i = 0;

    mtx_lock(&ar_pci->ce_lock);

    nentries_mask = src_ring->nentries_mask;
    sw_index = src_ring->sw_index;
    write_index = src_ring->write_index;

    if (unlikely(CE_RING_DELTA(nentries_mask, write_index, sw_index - 1) < (unsigned)n_items)) {
        err = ZX_ERR_NO_RESOURCES;
        goto err;
    }

    for (i = 0; i < n_items - 1; i++) {
        ath10k_dbg(ar, ATH10K_DBG_PCI, "pci tx item %d paddr 0x%08x len %d n_items %d\n", i,
                   items[i].paddr, items[i].len, n_items);
        ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci tx data: ", items[i].vaddr,
                        items[i].len);

        err = ath10k_ce_send_nolock(ce_pipe, items[i].transfer_context, items[i].paddr,
                                    items[i].len, items[i].transfer_id, CE_SEND_FLAG_GATHER);
        if (err != ZX_OK) { goto err; }
    }

    /* `i` is equal to `n_items -1` after for() */

    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci tx item %d paddr 0x%08x len %d n_items %d\n", i,
               items[i].paddr, items[i].len, n_items);
    ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci tx data: ", items[i].vaddr, items[i].len);

    err = ath10k_ce_send_nolock(ce_pipe, items[i].transfer_context, items[i].paddr, items[i].len,
                                items[i].transfer_id, 0);
    if (err) { goto err; }

    mtx_unlock(&ar_pci->ce_lock);
    return ZX_OK;

err:
    for (; i > 0; i--) {
        __ath10k_ce_send_revert(ce_pipe);
    }

    mtx_unlock(&ar_pci->ce_lock);
    return err;
}

zx_status_t ath10k_pci_hif_diag_read(struct ath10k* ar, uint32_t address, void* buf,
                                     size_t buf_len) {
    return ath10k_pci_diag_read_mem(ar, address, buf, buf_len);
}

uint16_t ath10k_pci_hif_get_free_queue_number(struct ath10k* ar, uint8_t pipe) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif get free queue number\n");

    return ath10k_ce_num_free_src_entries(ar_pci->pipe_info[pipe].ce_hdl);
}

#if 0   // NEEDS PORTING
static void ath10k_pci_dump_registers(struct ath10k* ar,
                                      struct ath10k_fw_crash_data* crash_data) {
    uint32_t reg_dump_values[REG_DUMP_COUNT_QCA988X] = {};
    int i;
    zx_status_t ret;

    ASSERT_MTX_HELD(&ar->data_lock);

    ret = ath10k_pci_diag_read_hi(ar, &reg_dump_values[0],
                                  hi_failure_state,
                                  REG_DUMP_COUNT_QCA988X * sizeof(uint32_t));
    if (ret != ZX_OK) {
        ath10k_err("failed to read firmware dump area: %s\n", zx_status_get_string(ret));
        return;
    }

    BUILD_BUG_ON(REG_DUMP_COUNT_QCA988X % 4);

    ath10k_err("firmware register dump:\n");
    for (i = 0; i < REG_DUMP_COUNT_QCA988X; i += 4)
        ath10k_err("[%02d]: 0x%08X 0x%08X 0x%08X 0x%08X\n",
                   i,
                   reg_dump_values[i],
                   reg_dump_values[i + 1],
                   reg_dump_values[i + 2],
                   reg_dump_values[i + 3]);

    if (!crash_data) {
        return;
    }

    for (i = 0; i < REG_DUMP_COUNT_QCA988X; i++) {
        crash_data->registers[i] = reg_dump_values[i];
    }
}
#endif  // NEEDS PORTING

static void ath10k_pci_fw_crashed_dump(struct ath10k* ar) {
#if 0   // NEEDS PORTING
    struct ath10k_fw_crash_data* crash_data;
    char uuid[50];

    mtx_lock(&ar->data_lock);

    ar->stats.fw_crash_counter++;

    crash_data = ath10k_debug_get_new_fw_crash_data(ar);

    if (crash_data) {
        snprintf(uuid, sizeof(uuid), "%pUl", &crash_data->uuid);
    } else {
        snprintf(uuid, sizeof(uuid), "n/a");
    }

    ath10k_err("firmware crashed! (uuid %s)\n", uuid);
    ath10k_print_driver_info(ar);
    ath10k_pci_dump_registers(ar, crash_data);
    ath10k_ce_dump_registers(ar, crash_data);

    mtx_unlock(&ar->data_lock);

    queue_work(ar->workqueue, &ar->restart_work);
#endif  // NEEDS PORTING
}

void ath10k_pci_hif_send_complete_check(struct ath10k* ar, uint8_t pipe, int force) {
    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif send complete check\n");

    if (!force) {
        int resources;
        /*
         * Decide whether to actually poll for completions, or just
         * wait for a later chance.
         * If there seem to be plenty of resources left, then just wait
         * since checking involves reading a CE register, which is a
         * relatively expensive operation.
         */
        resources = ath10k_pci_hif_get_free_queue_number(ar, pipe);

        /*
         * If at least 50% of the total resources are still available,
         * don't bother checking again yet.
         */
        if ((unsigned)resources > (host_ce_config_wlan[pipe].src_nentries >> 1)) { return; }
    }
    ath10k_ce_per_engine_service(ar, pipe);
}

zx_status_t ath10k_pci_hif_map_service_to_pipe(struct ath10k* ar, uint16_t service_id,
                                               uint8_t* ul_pipe, uint8_t* dl_pipe) {
    const struct service_to_pipe* entry;
    bool ul_set = false, dl_set = false;
    unsigned int i;

    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif map service\n");

    for (i = 0; i < countof(target_service_to_ce_map_wlan); i++) {
        entry = &target_service_to_ce_map_wlan[i];

        if (entry->service_id != service_id) { continue; }

        switch (entry->pipedir) {
        case PIPEDIR_NONE:
            break;
        case PIPEDIR_IN:
            COND_WARN(dl_set);
            *dl_pipe = entry->pipenum;
            dl_set = true;
            break;
        case PIPEDIR_OUT:
            COND_WARN(ul_set);
            *ul_pipe = entry->pipenum;
            ul_set = true;
            break;
        case PIPEDIR_INOUT:
            COND_WARN(dl_set);
            COND_WARN(ul_set);
            *dl_pipe = entry->pipenum;
            *ul_pipe = entry->pipenum;
            dl_set = true;
            ul_set = true;
            break;
        }
    }

    if (COND_WARN(!ul_set || !dl_set)) { return ZX_ERR_NOT_FOUND; }

    return ZX_OK;
}

void ath10k_pci_hif_get_default_pipe(struct ath10k* ar, uint8_t* ul_pipe, uint8_t* dl_pipe) {
    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif get default pipe\n");

    (void)ath10k_pci_hif_map_service_to_pipe(ar, ATH10K_HTC_SVC_ID_RSVD_CTRL, ul_pipe, dl_pipe);
}

void ath10k_pci_irq_msi_fw_mask(struct ath10k* ar) {
    uint32_t val;

    switch (ar->hw_rev) {
    case ATH10K_HW_QCA988X:
    case ATH10K_HW_QCA9887:
    case ATH10K_HW_QCA6174:
    case ATH10K_HW_QCA9377:
        val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS);
        val &= ~CORE_CTRL_PCIE_REG_31_MASK;
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS, val);
        break;
    case ATH10K_HW_QCA99X0:
    case ATH10K_HW_QCA9984:
    case ATH10K_HW_QCA9888:
    case ATH10K_HW_QCA4019:
        /* TODO: Find appropriate register configuration for QCA99X0
         *  to mask irq/MSI.
         */
        break;
    }
}

static void ath10k_pci_irq_msi_fw_unmask(struct ath10k* ar) {
    uint32_t val;

    switch (ar->hw_rev) {
    case ATH10K_HW_QCA988X:
    case ATH10K_HW_QCA9887:
    case ATH10K_HW_QCA6174:
    case ATH10K_HW_QCA9377:
        val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS);
        val |= CORE_CTRL_PCIE_REG_31_MASK;
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS, val);
        break;
    case ATH10K_HW_QCA99X0:
    case ATH10K_HW_QCA9984:
    case ATH10K_HW_QCA9888:
    case ATH10K_HW_QCA4019:
        /* TODO: Find appropriate register configuration for QCA99X0
         *  to unmask irq/MSI.
         */
        break;
    }
}

static void ath10k_pci_irq_disable(struct ath10k* ar) {
    ath10k_ce_disable_interrupts(ar);
    ath10k_pci_disable_and_clear_legacy_irq(ar);
    ath10k_pci_irq_msi_fw_mask(ar);
}

static void ath10k_pci_irq_sync(struct ath10k* ar) {
#if 0   // NEEDS PORTING
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    synchronize_irq(ar_pci->pdev->irq);
#endif  // NEEDS PORTING
}

static void ath10k_pci_irq_enable(struct ath10k* ar) {
    ath10k_ce_enable_interrupts(ar);
    ath10k_pci_enable_legacy_irq(ar);
    ath10k_pci_irq_msi_fw_unmask(ar);
}

static zx_status_t ath10k_pci_hif_start(struct ath10k* ar) {
    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif start\n");

    ath10k_pci_irq_enable(ar);
    ath10k_pci_rx_post(ar);

#if 0   // NEEDS PORTING
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    pcie_capability_write_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                               ar_pci->link_ctl);
#endif  // NEEDS PORTING

    return ZX_OK;
}

static void ath10k_pci_rx_pipe_cleanup(struct ath10k_pci_pipe* pci_pipe) {
    struct ath10k* ar;
    struct ath10k_ce_pipe* ce_pipe;
    struct ath10k_ce_ring* ce_ring;
    struct ath10k_msg_buf* buf;
    unsigned int i;

    ar = pci_pipe->hif_ce_state;
    ce_pipe = pci_pipe->ce_hdl;
    ce_ring = ce_pipe->dest_ring;

    if (!ce_ring) { return; }

    if (!pci_pipe->buf_sz) { return; }

    for (i = 0; i < ce_ring->nentries; i++) {
        buf = ce_ring->per_transfer_context[i];
        if (!buf) { continue; }

        ce_ring->per_transfer_context[i] = NULL;
        ath10k_msg_buf_free(buf);
    }
}

static void ath10k_pci_tx_pipe_cleanup(struct ath10k_pci_pipe* pci_pipe) {
    struct ath10k* ar;
    struct ath10k_ce_pipe* ce_pipe;
    struct ath10k_ce_ring* ce_ring;
    struct ath10k_msg_buf* buf;
    unsigned int i;

    ar = pci_pipe->hif_ce_state;
    ce_pipe = pci_pipe->ce_hdl;
    ce_ring = ce_pipe->src_ring;

    if (!ce_ring) { return; }

    if (!pci_pipe->buf_sz) { return; }

    for (i = 0; i < ce_ring->nentries; i++) {
        buf = ce_ring->per_transfer_context[i];
        if (!buf) { continue; }

        ce_ring->per_transfer_context[i] = NULL;

        ath10k_htc_tx_completion_handler(ar, buf);
    }
}

/*
 * Cleanup residual buffers for device shutdown:
 *    buffers that were enqueued for receive
 *    buffers that were to be sent
 * Note: Buffers that had completed but which were
 * not yet processed are on a completion queue. They
 * are handled when the completion thread shuts down.
 */
static void ath10k_pci_buffer_cleanup(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    int pipe_num;

    for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
        struct ath10k_pci_pipe* pipe_info;

        pipe_info = &ar_pci->pipe_info[pipe_num];
        ath10k_pci_rx_pipe_cleanup(pipe_info);
        ath10k_pci_tx_pipe_cleanup(pipe_info);
    }
}

void ath10k_pci_ce_deinit(struct ath10k* ar) {
    int i;

    for (i = 0; i < CE_COUNT; i++) {
        ath10k_ce_deinit_pipe(ar, i);
    }
}

void ath10k_pci_flush(struct ath10k* ar) {
    ath10k_pci_buffer_cleanup(ar);
}

static void ath10k_pci_hif_stop(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif stop\n");

    /* Most likely the device has HTT Rx ring configured. The only way to
     * prevent the device from accessing (and possible corrupting) host
     * memory is to reset the chip now.
     *
     * There's also no known way of masking MSI interrupts on the device.
     * For ranged MSI the CE-related interrupts can be masked. However
     * regardless how many MSI interrupts are assigned the first one
     * is always used for firmware indications (crashes) and cannot be
     * masked. To prevent the device from asserting the interrupt reset it
     * before proceeding with cleanup.
     */
    ath10k_pci_safe_chip_reset(ar);

    ath10k_pci_irq_disable(ar);
    ath10k_pci_irq_sync(ar);
    ath10k_pci_flush(ar);

    mtx_lock(&ar_pci->ps_lock);
    COND_WARN(ar_pci->ps_wake_refcount > 0);
    mtx_unlock(&ar_pci->ps_lock);
}

zx_status_t ath10k_pci_hif_exchange_bmi_msg(struct ath10k* ar, void* req, uint32_t req_len,
                                            void* resp, uint32_t* resp_len) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pci_tx = &ar_pci->pipe_info[BMI_CE_NUM_TO_TARG];
    struct ath10k_pci_pipe* pci_rx = &ar_pci->pipe_info[BMI_CE_NUM_TO_HOST];
    struct ath10k_ce_pipe* ce_tx = pci_tx->ce_hdl;
    struct ath10k_ce_pipe* ce_rx = pci_rx->ce_hdl;
    zx_paddr_t req_paddr = 0;
    zx_paddr_t resp_paddr = 0;
    struct bmi_xfer xfer = {};
    struct ath10k_msg_buf *treq, *tresp;
    void *req_vaddr, *resp_vaddr = NULL;
    zx_status_t ret = ZX_OK;

    if (resp && !resp_len) { return ZX_ERR_INVALID_ARGS; }

    if (resp && resp_len && *resp_len == 0) { return ZX_ERR_INVALID_ARGS; }

    ret = ath10k_msg_buf_alloc(ar, &treq, ATH10K_MSG_TYPE_BASE, req_len);
    if (ret != ZX_OK) { return ret; }
    req_vaddr = treq->vaddr;
    memcpy(req_vaddr, req, req_len);

    req_paddr = treq->paddr;
    ZX_DEBUG_ASSERT((req_paddr + req_len) <= 0x100000000ULL);

    if (resp && resp_len) {
        ret = ath10k_msg_buf_alloc(ar, &tresp, ATH10K_MSG_TYPE_BASE, *resp_len);
        if (ret != ZX_OK) { goto err_req; }
        resp_vaddr = tresp->vaddr;
        resp_paddr = tresp->paddr;

        xfer.wait_for_resp = true;
        xfer.resp_len = 0;

        ath10k_ce_rx_post_buf(ce_rx, &xfer, resp_paddr);
    }

    ret = ath10k_ce_send(ce_tx, &xfer, req_paddr, req_len, -1, 0);
    if (ret != ZX_OK) { goto err_resp; }

    ret = ath10k_pci_bmi_wait(ar, ce_tx, ce_rx, &xfer);
    if (ret != ZX_OK) {
        uint32_t unused_buffer;
        unsigned int unused_nbytes;
        unsigned int unused_id;

        ath10k_ce_cancel_send_next(ce_tx, NULL, &unused_buffer, &unused_nbytes, &unused_id);
    } else {
        /* non-zero means we did not time out */
        ret = ZX_OK;
    }

err_resp:
    if (resp) {
        uint32_t unused_buffer;

        ath10k_ce_revoke_recv_next(ce_rx, NULL, &unused_buffer);
    }
err_req:
    if (ret == ZX_OK && resp_len) {
        *resp_len = MIN(*resp_len, xfer.resp_len);
        memcpy(resp, resp_vaddr, xfer.resp_len);
    }
    ath10k_msg_buf_free(treq);
    if (resp_vaddr != NULL) { ath10k_msg_buf_free(tresp); }

    return ret;
}

static void ath10k_pci_bmi_send_done(struct ath10k_ce_pipe* ce_state) {
    struct bmi_xfer* xfer;

    if (ath10k_ce_completed_send_next(ce_state, (void**)&xfer)) { return; }

    xfer->tx_done = true;
}

static void ath10k_pci_bmi_recv_data(struct ath10k_ce_pipe* ce_state) {
    struct bmi_xfer* xfer;
    unsigned int nbytes;

    if (ath10k_ce_completed_recv_next(ce_state, (void**)&xfer, &nbytes)) { return; }

    if (COND_WARN_ONCE(!xfer)) { return; }

    if (!xfer->wait_for_resp) {
        ath10k_warn("unexpected: BMI data received; ignoring\n");
        return;
    }

    xfer->resp_len = nbytes;
    xfer->rx_done = true;
}

static zx_status_t ath10k_pci_bmi_wait(struct ath10k* ar, struct ath10k_ce_pipe* tx_pipe,
                                       struct ath10k_ce_pipe* rx_pipe, struct bmi_xfer* xfer) {
    zx_time_t started = zx_clock_get(ZX_CLOCK_MONOTONIC);
    zx_time_t timeout = started + BMI_COMMUNICATION_TIMEOUT;
    zx_time_t now, dur;
    zx_status_t ret;

    do {
        ath10k_pci_bmi_send_done(tx_pipe);
        ath10k_pci_bmi_recv_data(rx_pipe);

        if (xfer->tx_done && (xfer->rx_done == xfer->wait_for_resp)) {
            ret = ZX_OK;
            goto out;
        }

        thrd_yield();

        now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    } while (now < timeout);

    ret = ZX_ERR_TIMED_OUT;

out:
    dur = now - started;
    if (dur > ZX_SEC(1)) {
        double secs_elapsed = (double)(now - started) / ZX_SEC(1);
        ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi cmd took %0.2d secs, failed with %s\n", secs_elapsed,
                   zx_status_get_string(ret));
    }
    return ret;
}

/*
 * Send an interrupt to the device to wake up the Target CPU
 * so it has an opportunity to notice any changed state.
 */
static zx_status_t ath10k_pci_wake_target_cpu(struct ath10k* ar) {
    uint32_t addr, val;

    addr = SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS;
    val = ath10k_pci_read32(ar, addr);
    val |= CORE_CTRL_CPU_INTR_MASK;
    ath10k_pci_write32(ar, addr, val);

    return ZX_OK;
}

static int ath10k_pci_get_num_banks(struct ath10k* ar) {
    switch (ar->id.device) {
    case QCA988X_2_0_DEVICE_ID:
    case QCA99X0_2_0_DEVICE_ID:
    case QCA9888_2_0_DEVICE_ID:
    case QCA9984_1_0_DEVICE_ID:
    case QCA9887_1_0_DEVICE_ID:
        return 1;
    case QCA6164_2_1_DEVICE_ID:
    case QCA6174_2_1_DEVICE_ID:
        switch (MS(ar->chip_id, SOC_CHIP_ID_REV)) {
        case QCA6174_HW_1_0_CHIP_ID_REV:
        case QCA6174_HW_1_1_CHIP_ID_REV:
        case QCA6174_HW_2_1_CHIP_ID_REV:
        case QCA6174_HW_2_2_CHIP_ID_REV:
            return 3;
        case QCA6174_HW_1_3_CHIP_ID_REV:
            return 2;
        case QCA6174_HW_3_0_CHIP_ID_REV:
        case QCA6174_HW_3_1_CHIP_ID_REV:
        case QCA6174_HW_3_2_CHIP_ID_REV:
            return 9;
        }
        break;
    case QCA9377_1_0_DEVICE_ID:
        return 4;
    }

    ath10k_warn("unknown number of banks, assuming 1\n");
    return 1;
}

static int ath10k_bus_get_num_banks(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    return ar_pci->bus_ops->get_num_banks(ar);
}

zx_status_t ath10k_pci_init_config(struct ath10k* ar) {
    uint32_t interconnect_targ_addr;
    uint32_t pcie_state_targ_addr = 0;
    uint32_t pipe_cfg_targ_addr = 0;
    uint32_t svc_to_pipe_map = 0;
    uint32_t pcie_config_flags = 0;
    uint32_t ealloc_value;
    uint32_t ealloc_targ_addr;
    uint32_t flag2_value;
    uint32_t flag2_targ_addr;
    zx_status_t ret = ZX_OK;

    /* Download to Target the CE Config and the service-to-CE map */
    interconnect_targ_addr = host_interest_item_address(HI_ITEM(hi_interconnect_state));

    /* Supply Target-side CE configuration */
    ret = ath10k_pci_diag_read32(ar, interconnect_targ_addr, &pcie_state_targ_addr);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get pcie state addr: %s\n", zx_status_get_string(ret));
        return ret;
    }

    if (pcie_state_targ_addr == 0) {
        ret = ZX_ERR_IO;
        ath10k_err("Invalid pcie state addr\n");
        return ret;
    }

    ret = ath10k_pci_diag_read32(
        ar, (pcie_state_targ_addr + offsetof(struct pcie_state, pipe_cfg_addr)),
        &pipe_cfg_targ_addr);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get pipe cfg addr: %s\n", zx_status_get_string(ret));
        return ret;
    }

    if (pipe_cfg_targ_addr == 0) {
        ret = ZX_ERR_IO;
        ath10k_err("Invalid pipe cfg addr\n");
        return ret;
    }

    ret = ath10k_pci_diag_write_mem(ar, pipe_cfg_targ_addr, target_ce_config_wlan,
                                    sizeof(struct ce_pipe_config) * NUM_TARGET_CE_CONFIG_WLAN);

    if (ret != ZX_OK) {
        ath10k_err("Failed to write pipe cfg: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_diag_read32(
        ar, (pcie_state_targ_addr + offsetof(struct pcie_state, svc_to_pipe_map)),
        &svc_to_pipe_map);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get svc/pipe map: %s\n", zx_status_get_string(ret));
        return ret;
    }

    if (svc_to_pipe_map == 0) {
        ret = ZX_ERR_IO;
        ath10k_err("Invalid svc_to_pipe map\n");
        return ret;
    }

    ret = ath10k_pci_diag_write_mem(ar, svc_to_pipe_map, target_service_to_ce_map_wlan,
                                    sizeof(target_service_to_ce_map_wlan));
    if (ret != ZX_OK) {
        ath10k_err("Failed to write svc/pipe map: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_diag_read32(
        ar, (pcie_state_targ_addr + offsetof(struct pcie_state, config_flags)), &pcie_config_flags);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get pcie config_flags: %s\n", zx_status_get_string(ret));
        return ret;
    }

    pcie_config_flags &= ~PCIE_CONFIG_FLAG_ENABLE_L1;

    ret = ath10k_pci_diag_write32(
        ar, (pcie_state_targ_addr + offsetof(struct pcie_state, config_flags)), pcie_config_flags);
    if (ret != ZX_OK) {
        ath10k_err("Failed to write pcie config_flags: %s\n", zx_status_get_string(ret));
        return ret;
    }

    /* configure early allocation */
    ealloc_targ_addr = host_interest_item_address(HI_ITEM(hi_early_alloc));

    ret = ath10k_pci_diag_read32(ar, ealloc_targ_addr, &ealloc_value);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get early alloc val: %s\n", zx_status_get_string(ret));
        return ret;
    }

    /* first bank is switched to IRAM */
    ealloc_value |=
        ((HI_EARLY_ALLOC_MAGIC << HI_EARLY_ALLOC_MAGIC_SHIFT) & HI_EARLY_ALLOC_MAGIC_MASK);
    ealloc_value |= ((ath10k_bus_get_num_banks(ar) << HI_EARLY_ALLOC_IRAM_BANKS_SHIFT) &
                     HI_EARLY_ALLOC_IRAM_BANKS_MASK);

    ret = ath10k_pci_diag_write32(ar, ealloc_targ_addr, ealloc_value);
    if (ret != ZX_OK) {
        ath10k_err("Failed to set early alloc val: %s\n", zx_status_get_string(ret));
        return ret;
    }

    /* Tell Target to proceed with initialization */
    flag2_targ_addr = host_interest_item_address(HI_ITEM(hi_option_flag2));

    ret = ath10k_pci_diag_read32(ar, flag2_targ_addr, &flag2_value);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get option val: %s\n", zx_status_get_string(ret));
        return ret;
    }

    flag2_value |= HI_OPTION_EARLY_CFG_DONE;

    ret = ath10k_pci_diag_write32(ar, flag2_targ_addr, flag2_value);
    if (ret != ZX_OK) {
        ath10k_err("Failed to set option val: %s\n", zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

static void ath10k_pci_override_ce_config(struct ath10k* ar) {
    struct ce_attr* attr;
    struct ce_pipe_config* config;

    /* For QCA6174 we're overriding the Copy Engine 5 configuration,
     * since it is currently used for other feature.
     */

    /* Override Host's Copy Engine 5 configuration */
    attr = &host_ce_config_wlan[5];
    attr->src_sz_max = 0;
    attr->dest_nentries = 0;

    /* Override Target firmware's Copy Engine configuration */
    config = &target_ce_config_wlan[5];
    config->pipedir = PIPEDIR_OUT;
    config->nbytes_max = 2048;

    /* Map from service/endpoint to Copy Engine */
    target_service_to_ce_map_wlan[15].pipenum = 1;
}

zx_status_t ath10k_pci_alloc_pipes(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pipe;
    int i;
    zx_status_t ret;

    for (i = 0; i < CE_COUNT; i++) {
        pipe = &ar_pci->pipe_info[i];
        pipe->ce_hdl = &ar_pci->ce_states[i];
        pipe->pipe_num = i;
        pipe->hif_ce_state = ar;

        ret = ath10k_ce_alloc_pipe(ar, i, &host_ce_config_wlan[i]);
        if (ret != ZX_OK) {
            ath10k_err("failed to allocate copy engine pipe %d: %s\n", i,
                       zx_status_get_string(ret));
            return ret;
        }

        /* Last CE is Diagnostic Window */
        if (i == CE_DIAG_PIPE) {
            ar_pci->ce_diag = pipe->ce_hdl;
            continue;
        }

        pipe->buf_sz = (size_t)(host_ce_config_wlan[i].src_sz_max);
    }

    return ZX_OK;
}

void ath10k_pci_free_pipes(struct ath10k* ar) {
    int i;

    for (i = 0; i < CE_COUNT; i++) {
        ath10k_ce_free_pipe(ar, i);
    }
}

zx_status_t ath10k_pci_init_pipes(struct ath10k* ar) {
    int i;
    zx_status_t ret;

    for (i = 0; i < CE_COUNT; i++) {
        ret = ath10k_ce_init_pipe(ar, i, &host_ce_config_wlan[i]);
        if (ret != ZX_OK) {
            ath10k_err("failed to initialize copy engine pipe %d: %s\n", i,
                       zx_status_get_string(ret));
            return ret;
        }
    }

    return ZX_OK;
}

static bool ath10k_pci_has_fw_crashed(struct ath10k* ar) {
    return ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS) & FW_IND_EVENT_PENDING;
}

static void ath10k_pci_fw_crashed_clear(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);
    val &= ~FW_IND_EVENT_PENDING;
    ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, val);
}

static bool ath10k_pci_has_device_gone(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);
    return (val == 0xffffffff);
}

/* this function effectively clears target memory controller assert line */
static void ath10k_pci_warm_reset_si0(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
    ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS, val | SOC_RESET_CONTROL_SI0_RST_MASK);
    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
    ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS, val & ~SOC_RESET_CONTROL_SI0_RST_MASK);
    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
}

static void ath10k_pci_warm_reset_cpu(struct ath10k* ar) {
    uint32_t val;

    ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, 0);

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS);
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val | SOC_RESET_CONTROL_CPU_WARM_RST_MASK);
}

static void ath10k_pci_warm_reset_ce(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS);

    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val | SOC_RESET_CONTROL_CE_RST_MASK);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val & ~SOC_RESET_CONTROL_CE_RST_MASK);
}

static void ath10k_pci_warm_reset_clear_lf(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + SOC_LF_TIMER_CONTROL0_ADDRESS);
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_LF_TIMER_CONTROL0_ADDRESS,
                       val & ~SOC_LF_TIMER_CONTROL0_ENABLE_MASK);
}

static zx_status_t ath10k_pci_warm_reset(struct ath10k* ar) {
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot warm reset\n");

    mtx_lock(&ar->data_lock);
    ar->stats.fw_warm_reset_counter++;
    mtx_unlock(&ar->data_lock);

    ath10k_pci_irq_disable(ar);

    /* Make sure the target CPU is not doing anything dangerous, e.g. if it
     * were to access copy engine while host performs copy engine reset
     * then it is possible for the device to confuse pci-e controller to
     * the point of bringing host system to a complete stop (i.e. hang).
     */
    ath10k_pci_warm_reset_si0(ar);
    ath10k_pci_warm_reset_cpu(ar);
    ath10k_pci_init_pipes(ar);
    ath10k_pci_wait_for_target_init(ar);

    ath10k_pci_warm_reset_clear_lf(ar);
    ath10k_pci_warm_reset_ce(ar);
    ath10k_pci_warm_reset_cpu(ar);
    ath10k_pci_init_pipes(ar);

    ret = ath10k_pci_wait_for_target_init(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wait for target init: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot warm reset complete\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_qca99x0_soft_chip_reset(struct ath10k* ar) {
    ath10k_pci_irq_disable(ar);
    return ath10k_pci_qca99x0_chip_reset(ar);
}

static zx_status_t ath10k_pci_safe_chip_reset(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (!ar_pci->pci_soft_reset) { return ZX_ERR_NOT_SUPPORTED; }

    return ar_pci->pci_soft_reset(ar);
}

static zx_status_t ath10k_pci_qca988x_chip_reset(struct ath10k* ar) {
    int i;
    zx_status_t ret;
    uint32_t val;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot 988x chip reset\n");

    /* Some hardware revisions (e.g. CUS223v2) has issues with cold reset.
     * It is thus preferred to use warm reset which is safer but may not be
     * able to recover the device from all possible fail scenarios.
     *
     * Warm reset doesn't always work on first try so attempt it a few
     * times before giving up.
     */
    for (i = 0; i < ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS; i++) {
        ret = ath10k_pci_warm_reset(ar);
        if (ret != ZX_OK) {
            ath10k_warn("failed to warm reset attempt %d of %d: %s\n", i + 1,
                        ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS, zx_status_get_string(ret));
            continue;
        }

        /* FIXME: Sometimes copy engine doesn't recover after warm
         * reset. In most cases this needs cold reset. In some of these
         * cases the device is in such a state that a cold reset may
         * lock up the host.
         *
         * Reading any host interest register via copy engine is
         * sufficient to verify if device is capable of booting
         * firmware blob.
         */
        ret = ath10k_pci_init_pipes(ar);
        if (ret != ZX_OK) {
            ath10k_warn("failed to init copy engine: %s\n", zx_status_get_string(ret));
            continue;
        }

        ret = ath10k_pci_diag_read32(ar, QCA988X_HOST_INTEREST_ADDRESS, &val);
        if (ret != ZX_OK) {
            ath10k_warn("failed to poke copy engine: %s\n", zx_status_get_string(ret));
            continue;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot chip reset complete (warm)\n");
        return ZX_OK;
    }

    if (ath10k_pci_reset_mode == ATH10K_PCI_RESET_WARM_ONLY) {
        ath10k_warn("refusing cold reset as requested\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    ret = ath10k_pci_cold_reset(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_wait_for_target_init(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wait for target after cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca988x chip reset complete (cold)\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_qca6174_chip_reset(struct ath10k* ar) {
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca6174 chip reset\n");

    /* FIXME: QCA6174 requires cold + warm reset to work. */

    ret = ath10k_pci_cold_reset(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_wait_for_target_init(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wait for target after cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_warm_reset(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to warm reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca6174 chip reset complete (cold)\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_qca99x0_chip_reset(struct ath10k* ar) {
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca99x0 chip reset\n");

    ret = ath10k_pci_cold_reset(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_wait_for_target_init(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wait for target after cold reset: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca99x0 chip reset complete (cold)\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_chip_reset(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (COND_WARN(!ar_pci->pci_hard_reset)) { return ZX_ERR_NOT_SUPPORTED; }

    return ar_pci->pci_hard_reset(ar);
}

static zx_status_t ath10k_pci_hif_power_up(struct ath10k* ar) {
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power up\n");

#if 0   // TODO - get PCI express capability offset with kPciCapIdPciExpress and then
    //        read capability at offset + PCI_EXP_LNKCTL */
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    pcie_capability_read_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                              &ar_pci->link_ctl);
    pcie_capability_write_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                               ar_pci->link_ctl & ~PCI_EXP_LNKCTL_ASPMC);
#endif  // TODO

    /*
     * Bring the target up cleanly.
     *
     * The target may be in an undefined state with an AUX-powered Target
     * and a Host in WoW mode. If the Host crashes, loses power, or is
     * restarted (without unloading the driver) then the Target is left
     * (aux) powered and running. On a subsequent driver load, the Target
     * is in an unexpected state. We try to catch that here in order to
     * reset the Target and retry the probe.
     */
    ret = ath10k_pci_chip_reset(ar);
    if (ret != ZX_OK) {
        if (ath10k_pci_has_fw_crashed(ar)) {
            ath10k_warn("firmware crashed during chip reset\n");
            ath10k_pci_fw_crashed_clear(ar);
            ath10k_pci_fw_crashed_dump(ar);
        }

        ath10k_err("failed to reset chip: %s\n", zx_status_get_string(ret));
        goto err_sleep;
    }

    ret = ath10k_pci_init_pipes(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to initialize CE: %s\n", zx_status_get_string(ret));
        goto err_sleep;
    }

    ret = ath10k_pci_init_config(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to setup init config: %s\n", zx_status_get_string(ret));
        goto err_ce;
    }

    ret = ath10k_pci_wake_target_cpu(ar);
    if (ret != ZX_OK) {
        ath10k_err("could not wake up target CPU: %s\n", zx_status_get_string(ret));
        goto err_ce;
    }

    return ZX_OK;

err_ce:
    ath10k_pci_ce_deinit(ar);

err_sleep:
    return ret;
}

void ath10k_pci_hif_power_down(struct ath10k* ar) {
    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power down\n");

    /* Currently hif_power_up performs effectively a reset and hif_stop
     * resets the chip as well so there's no point in resetting here.
     */
}

#ifdef CONFIG_PM

static zx_status_t ath10k_pci_hif_suspend(struct ath10k* ar) {
    /* The grace timer can still be counting down and ar->ps_awake be true.
     * It is known that the device may be asleep after resuming regardless
     * of the SoC powersave state before suspending. Hence make sure the
     * device is asleep before proceeding.
     */
    ath10k_pci_sleep_sync(ar);

    return ZX_OK;
}

static zx_status_t ath10k_pci_hif_resume(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct pci_dev* pdev = ar_pci->pdev;
    uint32_t val;
    zx_status_t ret = ZX_OK;

    ret = ath10k_pci_force_wake(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to wake up target: %s\n", zx_status_get_string(ret));
        return ret;
    }

    /* Suspend/Resume resets the PCI configuration space, so we have to
     * re-disable the RETRY_TIMEOUT register (0x41) to keep PCI Tx retries
     * from interfering with C3 CPU state. pci_restore_state won't help
     * here since it only restores the first 64 bytes pci config header.
     */
    pci_read_config_dword(pdev, 0x40, &val);
    if ((val & 0x0000ff00) != 0) { pci_write_config_dword(pdev, 0x40, val & 0xffff00ff); }

    return ret;
}
#endif

static bool ath10k_pci_validate_cal(void* data, size_t size) {
    uint16_t* cal_words = data;
    uint16_t checksum = 0;
    size_t i;

    if (size % 2 != 0) { return false; }

    for (i = 0; i < size / 2; i++) {
        checksum ^= cal_words[i];
    }

    return checksum == 0xffff;
}

static void ath10k_pci_enable_eeprom(struct ath10k* ar) {
    /* Enable SI clock */
    ath10k_pci_soc_write32(ar, CLOCK_CONTROL_OFFSET, 0x0);

    /* Configure GPIOs for I2C operation */
    ath10k_pci_write32(
        ar, GPIO_BASE_ADDRESS + GPIO_PIN0_OFFSET + 4 * QCA9887_1_0_I2C_SDA_GPIO_PIN,
        SM(QCA9887_1_0_I2C_SDA_PIN_CONFIG, GPIO_PIN0_CONFIG) | SM(1, GPIO_PIN0_PAD_PULL));

    ath10k_pci_write32(
        ar, GPIO_BASE_ADDRESS + GPIO_PIN0_OFFSET + 4 * QCA9887_1_0_SI_CLK_GPIO_PIN,
        SM(QCA9887_1_0_SI_CLK_PIN_CONFIG, GPIO_PIN0_CONFIG) | SM(1, GPIO_PIN0_PAD_PULL));

    ath10k_pci_write32(ar, GPIO_BASE_ADDRESS + QCA9887_1_0_GPIO_ENABLE_W1TS_LOW_ADDRESS,
                       1u << QCA9887_1_0_SI_CLK_GPIO_PIN);

    /* In Swift ASIC - EEPROM clock will be (110MHz/512) = 214KHz */
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_CONFIG_OFFSET,
                       SM(1, SI_CONFIG_ERR_INT) | SM(1, SI_CONFIG_BIDIR_OD_DATA) |
                           SM(1, SI_CONFIG_I2C) | SM(1, SI_CONFIG_POS_SAMPLE) |
                           SM(1, SI_CONFIG_INACTIVE_DATA) | SM(1, SI_CONFIG_INACTIVE_CLK) |
                           SM(8, SI_CONFIG_DIVIDER));
}

static zx_status_t ath10k_pci_read_eeprom(struct ath10k* ar, uint16_t addr, uint8_t* out) {
    uint32_t reg;
    int wait_limit;

    /* set device select byte and for the read operation */
    reg = QCA9887_EEPROM_SELECT_READ | SM(addr, QCA9887_EEPROM_ADDR_LO) |
          SM(addr >> 8, QCA9887_EEPROM_ADDR_HI);
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_TX_DATA0_OFFSET, reg);

    /* write transmit data, transfer length, and START bit */
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_CS_OFFSET,
                       SM(1, SI_CS_START) | SM(1, SI_CS_RX_CNT) | SM(4, SI_CS_TX_CNT));

    /* wait max 1 sec */
    wait_limit = 100000;

    /* wait for SI_CS_DONE_INT */
    do {
        reg = ath10k_pci_read32(ar, SI_BASE_ADDRESS + SI_CS_OFFSET);
        if (MS(reg, SI_CS_DONE_INT)) { break; }

        wait_limit--;
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    } while (wait_limit > 0);

    if (!MS(reg, SI_CS_DONE_INT)) {
        ath10k_err("timeout while reading device EEPROM at %04x\n", addr);
        return ZX_ERR_TIMED_OUT;
    }

    /* clear SI_CS_DONE_INT */
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_CS_OFFSET, reg);

    if (MS(reg, SI_CS_DONE_ERR)) {
        ath10k_err("failed to read device EEPROM at %04x\n", addr);
        return ZX_ERR_IO;
    }

    /* extract receive data */
    reg = ath10k_pci_read32(ar, SI_BASE_ADDRESS + SI_RX_DATA0_OFFSET);
    *out = reg;

    return ZX_OK;
}

static zx_status_t ath10k_pci_hif_fetch_cal_eeprom(struct ath10k* ar, void** data,
                                                   size_t* data_len) {
    uint8_t* caldata = NULL;
    size_t calsize, i;
    zx_status_t ret;

    if (!QCA_REV_9887(ar)) { return ZX_ERR_NOT_SUPPORTED; }

    calsize = ar->hw_params.cal_data_len;
    caldata = malloc(calsize);
    if (!caldata) { return ZX_ERR_NO_MEMORY; }

    ath10k_pci_enable_eeprom(ar);

    for (i = 0; i < calsize; i++) {
        ret = ath10k_pci_read_eeprom(ar, i, &caldata[i]);
        if (ret != ZX_OK) { goto err_free; }
    }

    if (!ath10k_pci_validate_cal(caldata, calsize)) { goto err_free; }

    *data = caldata;
    *data_len = calsize;

    return ZX_OK;

err_free:
    free(caldata);

    return ZX_ERR_INVALID_ARGS;
}

static const struct ath10k_hif_ops ath10k_pci_hif_ops = {
    // clang-format off
    .tx_sg                 = ath10k_pci_hif_tx_sg,
    .diag_read             = ath10k_pci_hif_diag_read,
    .diag_write            = ath10k_pci_diag_write_mem,
    .get_bti_handle        = ath10k_pci_hif_get_bti_handle,
    .exchange_bmi_msg      = ath10k_pci_hif_exchange_bmi_msg,
    .start                 = ath10k_pci_hif_start,
    .stop                  = ath10k_pci_hif_stop,
    .map_service_to_pipe   = ath10k_pci_hif_map_service_to_pipe,
    .get_default_pipe      = ath10k_pci_hif_get_default_pipe,
    .send_complete_check   = ath10k_pci_hif_send_complete_check,
    .get_free_queue_number = ath10k_pci_hif_get_free_queue_number,
    .power_up              = ath10k_pci_hif_power_up,
    .power_down            = ath10k_pci_hif_power_down,
    .read32                = ath10k_pci_read32,
    .write32               = ath10k_pci_write32,
#ifdef CONFIG_PM
    .suspend               = ath10k_pci_hif_suspend,
    .resume                = ath10k_pci_hif_resume,
#endif
    .fetch_cal_eeprom      = ath10k_pci_hif_fetch_cal_eeprom,
    // clang-format on
};

static void ath10k_pci_interrupt_poll(struct ath10k* ar) {
    while (CE_INTERRUPT_SUMMARY(ar)) {
        ath10k_ce_per_engine_service_any(ar);
    }
}

static int ath10k_pci_interrupt_handler(void* arg) {
    struct ath10k* ar = arg;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t status;

    while ((status = zx_interrupt_wait(ar_pci->irq_handle, NULL)) == ZX_OK) {
        if (ath10k_pci_has_device_gone(ar)) {
            ath10k_warn("target is no longer present\n");
            break;
        }
        ath10k_pci_interrupt_poll(ar);
    }

    ath10k_err("ISR exiting with status %s\n", zx_status_get_string(status));
    return (int)status;
}

static zx_status_t ath10k_pci_request_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    pci_protocol_t* pdev = &ar_pci->pdev;
    zx_status_t ret;

    ret = pci_map_interrupt(pdev, 0, &ar_pci->irq_handle);
    if (ret != ZX_OK) { ath10k_err("couldn't map irq 0\n"); }
    return ret;
}

static void ath10k_pci_free_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    zx_handle_close(ar_pci->irq_handle);
}

static zx_status_t ath10k_pci_init_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    pci_protocol_t* pdev = &ar_pci->pdev;
    uint32_t irq_cnt = 0;

    if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_AUTO) {
        ath10k_trace("limiting irq mode to: %d\n", ath10k_pci_irq_mode);
    }

    /* Try MSI */
    if ((ath10k_pci_irq_mode != ATH10K_PCI_IRQ_LEGACY) &&
        (pci_query_irq_mode(pdev, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt) == ZX_OK) &&
        (pci_set_irq_mode(pdev, ZX_PCIE_IRQ_MODE_MSI, 1) == ZX_OK)) {
        ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_MSI;

        return ZX_OK;
    }

    /* Try legacy irq
     *
     * A potential race occurs here: The CORE_BASE write
     * depends on target correctly decoding AXI address but
     * host won't know when target writes BAR to CORE_CTRL.
     * This write might get lost if target has NOT written BAR.
     * For now, fix the race by repeating the write in below
     * synchronization checking.
     */
    if ((pci_query_irq_mode(pdev, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt) == ZX_OK) &&
        (pci_set_irq_mode(pdev, ZX_PCIE_IRQ_MODE_LEGACY, 1) == ZX_OK)) {
        ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_LEGACY;

        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                           PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

        return ZX_OK;
    }

    ath10k_err("failed to determine IRQ mode\n");
    return ZX_ERR_NOT_SUPPORTED;
}

static void ath10k_pci_deinit_irq_legacy(struct ath10k* ar) {
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS, 0);
}

static void ath10k_pci_deinit_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    switch (ar_pci->oper_irq_mode) {
    case ATH10K_PCI_IRQ_LEGACY:
        ath10k_pci_deinit_irq_legacy(ar);
        break;
    default:
#if 0   // TODO - How to disable MSI interrupts?
        pci_disable_msi(ar_pci->pdev);
#endif  // TODO
        break;
    }
}

zx_status_t ath10k_pci_wait_for_target_init(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_time_t timeout;
    uint32_t val;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot waiting target to initialise\n");

    timeout = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(ATH10K_PCI_TARGET_WAIT);

    do {
        val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target indicator %x\n", val);

        /* target should never return this */
        if (val == 0xffffffff) { continue; }

        /* the device has crashed so don't bother trying anymore */
        if (val & FW_IND_EVENT_PENDING) { break; }

        if (val & FW_IND_INITIALIZED) { break; }

        if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_LEGACY) {
            /* Fix potential race by repeating CORE_BASE writes */
            ath10k_pci_enable_legacy_irq(ar);
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    } while (zx_clock_get(ZX_CLOCK_MONOTONIC) < timeout);

    ath10k_pci_disable_and_clear_legacy_irq(ar);
    ath10k_pci_irq_msi_fw_mask(ar);

    if (val == 0xffffffff) {
        ath10k_err("failed to read device register, device is gone\n");
        return ZX_ERR_IO;
    }

    if (val & FW_IND_EVENT_PENDING) {
        ath10k_warn("device has crashed during init\n");
        return ZX_ERR_UNAVAILABLE;
    }

    if (!(val & FW_IND_INITIALIZED)) {
        ath10k_err("failed to receive initialized event from target: %08x\n", val);
        return ZX_ERR_TIMED_OUT;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target initialised\n");
    return ZX_OK;
}

#if DEBUG_MSG_BUF
static int ath10k_monitor(void* arg) {
    struct ath10k* ar = arg;
    while (1) {
        zx_nanosleep(zx_deadline_after(ZX_SEC(5)));
        ath10k_msg_buf_dump_stats(ar);
        printf("  Interrupt status: %#x\n", CE_INTERRUPT_SUMMARY(ar));
    }
    return 0;
}
#endif

static zx_status_t ath10k_pci_cold_reset(struct ath10k* ar) {
    uint32_t val;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cold reset\n");

    mtx_lock(&ar->data_lock);

    ar->stats.fw_cold_reset_counter++;

    mtx_unlock(&ar->data_lock);

    /* Put Target, including PCIe, into RESET. */
    val = ath10k_pci_reg_read32(ar, SOC_GLOBAL_RESET_ADDRESS);
    val |= 1;
    ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

    /* After writing into SOC_GLOBAL_RESET to put device into
     * reset and pulling out of reset pcie may not be stable
     * for any immediate pcie register access and cause bus error,
     * add delay before any pcie access request to fix this issue.
     */
    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

    /* Pull Target, including PCIe, out of RESET. */
    val &= ~1;
    ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cold reset complete\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_claim(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    pci_protocol_t* pdev = &ar_pci->pdev;
    zx_status_t ret;

    ret = pci_map_bar(pdev, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &ar_pci->mem, &ar_pci->mem_len,
                      &ar_pci->mem_handle);
    if (ret != ZX_OK) {
        ath10k_err("failed to map resources for BAR 0: %s\n", zx_status_get_string(ret));
        goto err_device;
    }

    // TODO: Verify that the requested addresses are in 32b range
#if 0
    zx_paddr_t phys_addr;
    ret = zx_vmo_op_range(ar_pci->mem_handle, ZX_VMO_OP_LOOKUP, 0, 8, &phys_addr,
                          sizeof(zx_paddr_t));
    if (ret != ZX_OK) {
        ath10k_err("failed to get physical address of PCI mem\n");
        goto err_region;
    }
    if (phys_addr + ar_pci->mem_len > 0xffffffff) {
        ath10k_err("PCI mem allocated outside of 32-bit address space\n");
        ret = ZX_ERR_INTERNAL;
        goto err_region;
    }
#endif
    ret = pci_enable_bus_master(pdev, true);
    if (ret != ZX_OK) {
        ath10k_err("failed to enable bus mastering\n");
        goto err_region;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot pci_mem 0x%p\n", ar_pci->mem);
    return ZX_OK;

err_region:
    zx_handle_close(ar_pci->mem_handle);

err_device:
    return ret;
}

static void ath10k_pci_release(void* ctx) {
    struct ath10k* ath = ctx;
    // TODO - Clear mastering and releaes handles
    free(ath);
}

static bool ath10k_pci_chip_is_supported(uint32_t dev_id, uint32_t chip_id) {
    const struct ath10k_pci_supp_chip* supp_chip;
    unsigned long i;
    uint32_t rev_id = MS(chip_id, SOC_CHIP_ID_REV);

    for (i = 0; i < countof(ath10k_pci_supp_chips); i++) {
        supp_chip = &ath10k_pci_supp_chips[i];

        if (supp_chip->dev_id == dev_id && supp_chip->rev_id == rev_id) { return true; }
    }

    return false;
}

zx_status_t ath10k_pci_setup_resource(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    mtx_init(&ar_pci->ce_lock, mtx_plain);
#if 0   // NEEDS PORTING
    mtx_init(&ar_pci->ps_lock, mtx_plain);
#endif  // NEEDS PORTING

    if (QCA_REV_6174(ar) || QCA_REV_9377(ar)) { ath10k_pci_override_ce_config(ar); }

    ret = ath10k_pci_alloc_pipes(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to allocate copy engine pipes: %s\n", zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

void ath10k_pci_release_resource(struct ath10k* ar) {
    ath10k_pci_ce_deinit(ar);
    ath10k_pci_free_pipes(ar);
}

static const struct ath10k_bus_ops ath10k_pci_bus_ops = {
    .read32 = ath10k_bus_pci_read32,
    .write32 = ath10k_bus_pci_write32,
    .get_num_banks = ath10k_pci_get_num_banks,
};

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = ath10k_pci_release,
};

static void ath10k_chan_query_info(const struct ath10k_channel* dev_channel, void* cookie) {
    uint8_t** wlan_channel_ptr = cookie;
    *(*wlan_channel_ptr)++ = dev_channel->hw_value;
}

static void ath10k_band_query_info(const struct ath10k_band* dev_band, void* cookie) {
    wlanmac_info_t* info = cookie;
    wlan_info_t* ifc_info = &info->ifc_info;
    wlan_band_info_t* wlan_band = &ifc_info->bands[ifc_info->num_bands++];
    strncpy(wlan_band->desc, dev_band->name, WLAN_BAND_DESC_MAX_LEN);
    ZX_DEBUG_ASSERT(sizeof(wlan_band->ht_caps) == sizeof(dev_band->ht_caps));
    memcpy(&wlan_band->ht_caps, &dev_band->ht_caps, sizeof(wlan_band->ht_caps));
    wlan_band->vht_supported = dev_band->vht_supported;
    ZX_DEBUG_ASSERT(sizeof(wlan_band->vht_caps) == sizeof(dev_band->vht_caps));
    memcpy(&wlan_band->vht_caps, &dev_band->vht_caps, sizeof(wlan_band->vht_caps));
    ZX_DEBUG_ASSERT(sizeof(wlan_band->basic_rates) == sizeof(dev_band->basic_rates));
    memcpy(&wlan_band->basic_rates, &dev_band->basic_rates, sizeof(wlan_band->basic_rates));
    wlan_band->supported_channels.base_freq = dev_band->base_freq;

    uint8_t* next_ch = wlan_band->supported_channels.channels;
    ath10k_foreach_channel(dev_band, ath10k_chan_query_info, &next_ch);
}

static zx_status_t ath10k_pci_query(void* ctx, uint32_t options, wlanmac_info_t* info) {
    // TODO: ALL of the values below are hard-coded and faked for now.
    struct ath10k* ar = ctx;

    ZX_DEBUG_ASSERT(BITARR_TEST(ar->dev_flags, ATH10K_FLAG_CORE_REGISTERED));

    memset(info, 0, sizeof(*info));

    wlan_info_t* ifc_info = &info->ifc_info;

    // eth_info
    ZX_DEBUG_ASSERT(ETH_ALEN == ETH_MAC_SIZE);
    memcpy(ifc_info->mac_addr, ar->mac_addr, ETH_MAC_SIZE);

    // mac_role
    ifc_info->mac_role = WLAN_MAC_ROLE_CLIENT;

    // supported_phys
    ifc_info->supported_phys = WLAN_PHY_DSSS | WLAN_PHY_CCK | WLAN_PHY_OFDM;
    if (ar->ht_cap_info & WMI_HT_CAP_ENABLED) { ifc_info->supported_phys |= WLAN_PHY_HT; }
    ifc_info->supported_phys |= WLAN_PHY_VHT;

    // driver_features
    ifc_info->driver_features =
        WLAN_DRIVER_FEATURE_SCAN_OFFLOAD | WLAN_DRIVER_FEATURE_RATE_SELECTION;

    // caps
    ifc_info->caps = WLAN_CAP_SHORT_PREAMBLE | WLAN_CAP_SPECTRUM_MGMT | WLAN_CAP_SHORT_SLOT_TIME;

    // bands
    ath10k_foreach_band(ath10k_band_query_info, ifc_info);

    return ZX_OK;
}

static zx_status_t ath10k_pci_start(void* ctx, wlanmac_ifc_t* ifc, void* cookie) {
    struct ath10k* ar = ctx;
    return ath10k_start(ar, ifc, cookie);
}

static void ath10k_pci_stop(void* ctx) {
    // TODO
}

static bool verify_started(struct ath10k* ar) {
    mtx_lock(&ar->conf_mutex);
    bool result = (ar->state == ATH10K_STATE_ON);
    mtx_unlock(&ar->conf_mutex);
    return result;
}

static zx_status_t ath10k_pci_queue_tx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
    struct ath10k* ar = ctx;
    return ath10k_mac_op_tx(ar, pkt);
}

static const char* cbw_as_str(uint8_t cbw) {
    switch (cbw) {
    case CBW20:
        return "CBW20";
    case CBW40:
        return "CBW40ABOVE";
    case CBW40BELOW:
        return "CBW40BELOW";
    case CBW80:
        return "CBW80";
    case CBW160:
        return "CBW160";
    case CBW80P80:
        return "CBW80P80";
    default:
        return "Invalid";
    }
}

static zx_status_t ath10k_pci_set_channel(void* ctx, uint32_t options, wlan_channel_t* chan) {
    struct ath10k* ar = ctx;
    if (!verify_started(ar)) { return ZX_ERR_BAD_STATE; }

    ath10k_trace("setting channel (pri: %d, sec: %d, bw: %s)\n", chan->primary, chan->secondary80,
                 cbw_as_str(chan->cbw));
    memcpy(&ar->rx_channel, chan, sizeof(wlan_channel_t));
    return ath10k_mac_assign_vif_chanctx(ar, chan);
}

static zx_status_t ath10k_pci_configure_bss(void* ctx, uint32_t options,
                                            wlan_bss_config_t* config) {
    struct ath10k* ar = ctx;
    if (!verify_started(ar)) { return ZX_ERR_BAD_STATE; }

    return ath10k_mac_set_bss(ar, config);
}

static zx_status_t ath10k_pci_set_key(void* ctx, uint32_t options, wlan_key_config_t* key_config) {
    struct ath10k* ar = ctx;
    ath10k_info(
        "attempting to set key (prot: %s, cipher: %s, type: %s, len: %d,"
        " addr: %02x:%02x:%02x:%02x:%02x:%02x key_idx: %d)\n",
        key_config->protection == WLAN_PROTECTION_NONE
            ? "none"
            : key_config->protection == WLAN_PROTECTION_RX
                  ? "rx"
                  : key_config->protection == WLAN_PROTECTION_TX
                        ? "rx"
                        : key_config->protection == WLAN_PROTECTION_RX_TX ? "rx/tx" : "unknown",
        ieee80211_cipher_str(key_config->cipher_oui, key_config->cipher_type),
        key_config->key_type == WLAN_KEY_TYPE_PAIRWISE
            ? "pairwise"
            : key_config->key_type == WLAN_KEY_TYPE_GROUP
                  ? "group"
                  : key_config->key_type == WLAN_KEY_TYPE_IGTK
                        ? "IGTK"
                        : key_config->key_type == WLAN_KEY_TYPE_PEER ? "peer" : "unknown",
        key_config->key_len, key_config->peer_addr[0], key_config->peer_addr[1],
        key_config->peer_addr[2], key_config->peer_addr[3], key_config->peer_addr[4],
        key_config->peer_addr[5], key_config->key_idx);
    return ath10k_mac_set_key(ar, key_config);
}

static zx_status_t ath10k_pci_configure_assoc(void* ctx, uint32_t options,
                                              wlan_assoc_ctx_t* assoc_ctx) {
    // TODO(NET-818): Implement
    return ZX_OK;
}

static wlanmac_protocol_ops_t wlanmac_ops = {
    .query = ath10k_pci_query,
    .start = ath10k_pci_start,
    .stop = ath10k_pci_stop,
    .queue_tx = ath10k_pci_queue_tx,
    .set_channel = ath10k_pci_set_channel,
    .configure_bss = ath10k_pci_configure_bss,
    .set_key = ath10k_pci_set_key,
    .configure_assoc = ath10k_pci_configure_assoc,
};

static zx_status_t ath10k_pci_probe(void* ctx, zx_device_t* dev) {
    zx_status_t ret = ZX_OK;
    struct ath10k* ar;
    struct ath10k_pci* ar_pci;
    enum ath10k_hw_rev hw_rev;
    uint32_t chip_id;
    bool pci_ps;
    zx_status_t (*pci_soft_reset)(struct ath10k * ar);
    zx_status_t (*pci_hard_reset)(struct ath10k * ar);
    zx_status_t (*targ_cpu_to_ce_addr)(struct ath10k * ar, uint32_t addr, uint32_t * ce_addr);

    pci_protocol_t pci;
    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci) != ZX_OK) { return ZX_ERR_NOT_SUPPORTED; }

    zx_handle_t pci_btih;
    if (pci_get_bti(&pci, 0, &pci_btih) != ZX_OK) { return ZX_ERR_NOT_SUPPORTED; }

    zx_pcie_device_info_t pci_info;
    if (pci_get_device_info(&pci, &pci_info) != ZX_OK) { return ZX_ERR_NOT_SUPPORTED; }

    switch (pci_info.device_id) {
    case QCA988X_2_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA988X;
        pci_ps = false;
        pci_soft_reset = ath10k_pci_warm_reset;
        pci_hard_reset = ath10k_pci_qca988x_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
        break;
    case QCA9887_1_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA9887;
        pci_ps = false;
        pci_soft_reset = ath10k_pci_warm_reset;
        pci_hard_reset = ath10k_pci_qca988x_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
        break;
    case QCA6164_2_1_DEVICE_ID:
    case QCA6174_2_1_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA6174;
        pci_ps = true;
        pci_soft_reset = ath10k_pci_warm_reset;
        pci_hard_reset = ath10k_pci_qca6174_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
        break;
    case QCA99X0_2_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA99X0;
        pci_ps = false;
        pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
        pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
        break;
    case QCA9984_1_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA9984;
        pci_ps = false;
        pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
        pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
        break;
    case QCA9888_2_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA9888;
        pci_ps = false;
        pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
        pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
        break;
    case QCA9377_1_0_DEVICE_ID:
        hw_rev = ATH10K_HW_QCA9377;
        pci_ps = true;
        pci_soft_reset = NULL;
        pci_hard_reset = ath10k_pci_qca6174_chip_reset;
        targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
        break;
    default:
        ath10k_err("unrecognized device ID: %#0" PRIx16 "\n", pci_info.device_id);
        return ZX_ERR_NOT_SUPPORTED;
    }

    ret =
        ath10k_core_create(&ar, sizeof(*ar_pci), dev, ATH10K_BUS_PCI, hw_rev, &ath10k_pci_hif_ops);
    if (ret != ZX_OK) {
        ath10k_err("failed to create core\n");
        return ret;
    }

    uint16_t subsystem_vendor_id;
    ret = pci_config_read16(&pci, kPciCfgSubsystemVendorId, &subsystem_vendor_id);
    if (ret != ZX_OK) {
        ath10k_err("failed to read PCI subsystem vendor ID: %s\n", zx_status_get_string(ret));
    }

    uint16_t subsystem_device_id;
    ret = pci_config_read16(&pci, kPciCfgSubsystemId, &subsystem_device_id);
    if (ret != ZX_OK) {
        ath10k_err("failed to read PCI subsystem device ID: %s\n", zx_status_get_string(ret));
    }

    ath10k_trace("pci probe %04x:%04x %04x:%04x\n", pci_info.vendor_id, pci_info.device_id,
                 subsystem_vendor_id, subsystem_device_id);

    ar_pci = ath10k_pci_priv(ar);
    memcpy(&ar_pci->pdev, &pci, sizeof(ar_pci->pdev));
    ar_pci->btih = pci_btih;
    ar_pci->dev = dev;
    ar_pci->ar = ar;
    ar->dev_id = pci_info.device_id;
    ar_pci->pci_ps = pci_ps;
    ar_pci->bus_ops = &ath10k_pci_bus_ops;
    ar_pci->pci_soft_reset = pci_soft_reset;
    ar_pci->pci_hard_reset = pci_hard_reset;
    ar_pci->targ_cpu_to_ce_addr = targ_cpu_to_ce_addr;
    ar->id.vendor = pci_info.vendor_id;
    ar->id.device = pci_info.device_id;
    ar->id.subsystem_vendor = subsystem_vendor_id;
    ar->id.subsystem_device = subsystem_device_id;

    ret = ath10k_msg_bufs_init(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to initialize msg_bufs structures\n");
        return ret;
    }

    ret = ath10k_pci_setup_resource(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to setup resource: %s\n", zx_status_get_string(ret));
        goto err_core_destroy;
    }

    ret = ath10k_pci_claim(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to map PCI memory: %s\n", zx_status_get_string(ret));
        goto err_free_pipes;
    }

    ret = ath10k_pci_force_wake(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wake up device : %s\n", zx_status_get_string(ret));
        goto err_sleep;
    }

    ath10k_pci_ce_deinit(ar);
    ath10k_pci_irq_disable(ar);

    ret = ath10k_pci_init_irq(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to init irqs: %s\n", zx_status_get_string(ret));
        goto err_sleep;
    }

    ath10k_trace("pci irq %s oper_irq_mode %d irq_mode %d reset_mode %d\n",
                 ath10k_pci_get_irq_method(ar), ar_pci->oper_irq_mode, ath10k_pci_irq_mode,
                 ath10k_pci_reset_mode);

    ret = ath10k_pci_request_irq(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to request irqs: %s\n", zx_status_get_string(ret));
        goto err_deinit_irq;
    }

    thrd_create_with_name(&ar->isr_thread, ath10k_pci_interrupt_handler, ar, "ath10k-isr");
    thrd_detach(ar->isr_thread);

    ret = ath10k_pci_chip_reset(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to reset chip: %s\n", zx_status_get_string(ret));
        goto err_free_irq;
    }

    chip_id = ath10k_pci_soc_read32(ar, SOC_CHIP_ID_ADDRESS);
    if (chip_id == 0xffffffff) {
        ath10k_err("failed to get chip id\n");
        goto err_free_irq;
    }

    if (!ath10k_pci_chip_is_supported(pci_info.device_id, chip_id)) {
        ath10k_err("device %04x with chip_id %08x isn't supported\n", pci_info.device_id, chip_id);
        goto err_free_irq;
    }

#if DEBUG_MSG_BUF
    thrd_create_with_name(&ar->monitor_thread, ath10k_monitor, ar, "ath10k-monitor");
    thrd_detach(ar->monitor_thread);
#endif

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ath10k",
        .ctx = ar,
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_WLANMAC,
        .proto_ops = &wlanmac_ops,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    ret = device_add(dev, &args, &ar->zxdev);
    if (ret != ZX_OK) { goto err_free_irq; }

    ret = ath10k_core_register(ar, chip_id);
    if (ret != ZX_OK) {
        ath10k_err("failed to register driver core: %s\n", zx_status_get_string(ret));
        goto err_free_device;
    }

    return ZX_OK;

err_free_device:
    device_remove(dev);

err_free_irq:
    ath10k_pci_free_irq(ar);

err_deinit_irq:
    ath10k_pci_deinit_irq(ar);

err_sleep:
#if 0   // NEEDS PORTING
    ath10k_pci_sleep_sync(ar);
#endif  // NEEDS PORTING
    ath10k_pci_release(ar);

err_free_pipes:
    ath10k_pci_free_pipes(ar);

err_core_destroy:
    ath10k_core_destroy(ar);

    return ret;
}

#if 0   // NEEDS PORTING
static void ath10k_pci_remove(struct pci_dev* pdev) {
    struct ath10k* ar = pci_get_drvdata(pdev);
    struct ath10k_pci* ar_pci;

    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci remove\n");

    if (!ar) {
        return;
    }

    ar_pci = ath10k_pci_priv(ar);

    if (!ar_pci) {
        return;
    }

    ath10k_core_unregister(ar);
    ath10k_pci_free_irq(ar);
    ath10k_pci_deinit_irq(ar);
    ath10k_pci_release_resource(ar);
    ath10k_pci_sleep_sync(ar);
    ath10k_pci_release(ar);
    ath10k_core_destroy(ar);
}
#endif  // NEEDS PORTING

static zx_driver_ops_t ath10k_pci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ath10k_pci_probe,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(ath10k_pci, ath10k_pci_driver_ops, "zircon", "0.1", 10)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, ATHEROS_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA988X_2_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6174_2_1_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA99X0_2_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6164_2_1_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9377_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9984_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9887_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9888_2_0_DEVICE_ID),
ZIRCON_DRIVER_END(ath10k_pci)
