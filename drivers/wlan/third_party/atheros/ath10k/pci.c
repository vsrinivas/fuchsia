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

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include "macros.h"

#if 0 // NEEDS PORTING
#include "core.h"
#include "debug.h"

#include "targaddrs.h"
#include "bmi.h"

#include "hif.h"
#include "htc.h"

#include "ce.h"
#endif // NEEDS PORTING
#include "pci.h"
#if 0 // NEEDS PORTING

enum ath10k_pci_reset_mode {
    ATH10K_PCI_RESET_AUTO = 0,
    ATH10K_PCI_RESET_WARM_ONLY = 1,
};

// MODULE PARAMETERS
static unsigned int ath10k_pci_irq_mode = ATH10K_PCI_IRQ_AUTO;
static unsigned int ath10k_pci_reset_mode = ATH10K_PCI_RESET_AUTO;

/* how long wait to wait for target to initialise, in ms */
#define ATH10K_PCI_TARGET_WAIT 3000
#define ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS 3

static const struct pci_device_id ath10k_pci_id_table[] = {
    { PCI_VDEVICE(ATHEROS, QCA988X_2_0_DEVICE_ID) }, /* PCI-E QCA988X V2 */
    { PCI_VDEVICE(ATHEROS, QCA6164_2_1_DEVICE_ID) }, /* PCI-E QCA6164 V2.1 */
    { PCI_VDEVICE(ATHEROS, QCA6174_2_1_DEVICE_ID) }, /* PCI-E QCA6174 V2.1 */
    { PCI_VDEVICE(ATHEROS, QCA99X0_2_0_DEVICE_ID) }, /* PCI-E QCA99X0 V2 */
    { PCI_VDEVICE(ATHEROS, QCA9888_2_0_DEVICE_ID) }, /* PCI-E QCA9888 V2 */
    { PCI_VDEVICE(ATHEROS, QCA9984_1_0_DEVICE_ID) }, /* PCI-E QCA9984 V1 */
    { PCI_VDEVICE(ATHEROS, QCA9377_1_0_DEVICE_ID) }, /* PCI-E QCA9377 V1 */
    { PCI_VDEVICE(ATHEROS, QCA9887_1_0_DEVICE_ID) }, /* PCI-E QCA9887 */
    {0}
};

static const struct ath10k_pci_supp_chip ath10k_pci_supp_chips[] = {
    /* QCA988X pre 2.0 chips are not supported because they need some nasty
     * hacks. ath10k doesn't have them and these devices crash horribly
     * because of that.
     */
    { QCA988X_2_0_DEVICE_ID, QCA988X_HW_2_0_CHIP_ID_REV },

    { QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV },
    { QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV },
    { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV },
    { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV },
    { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV },

    { QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV },
    { QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV },
    { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV },
    { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV },
    { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV },

    { QCA99X0_2_0_DEVICE_ID, QCA99X0_HW_2_0_CHIP_ID_REV },

    { QCA9984_1_0_DEVICE_ID, QCA9984_HW_1_0_CHIP_ID_REV },

    { QCA9888_2_0_DEVICE_ID, QCA9888_HW_2_0_CHIP_ID_REV },

    { QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_0_CHIP_ID_REV },
    { QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_1_CHIP_ID_REV },

    { QCA9887_1_0_DEVICE_ID, QCA9887_HW_1_0_CHIP_ID_REV },
};

static void ath10k_pci_buffer_cleanup(struct ath10k* ar);
static zx_status_t ath10k_pci_cold_reset(struct ath10k* ar);
static zx_status_t ath10k_pci_safe_chip_reset(struct ath10k* ar);
static zx_status_t ath10k_pci_init_irq(struct ath10k* ar);
static zx_status_t ath10k_pci_deinit_irq(struct ath10k* ar);
static zx_status_t ath10k_pci_request_irq(struct ath10k* ar);
static void ath10k_pci_free_irq(struct ath10k* ar);
static zx_status_t ath10k_pci_bmi_wait(struct ath10k* ar,
                                       struct ath10k_ce_pipe* tx_pipe,
                                       struct ath10k_ce_pipe* rx_pipe,
                                       struct bmi_xfer* xfer);
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
        .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
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
        PIPEDIR_IN,  /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BK,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BK,
        PIPEDIR_IN,  /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BE,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_BE,
        PIPEDIR_IN,  /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VI,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_DATA_VI,
        PIPEDIR_IN,  /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_CONTROL,
        PIPEDIR_OUT, /* out = UL = host -> target */
        3,
    },
    {
        ATH10K_HTC_SVC_ID_WMI_CONTROL,
        PIPEDIR_IN,  /* in = DL = target -> host */
        2,
    },
    {
        ATH10K_HTC_SVC_ID_RSVD_CTRL,
        PIPEDIR_OUT, /* out = UL = host -> target */
        0,
    },
    {
        ATH10K_HTC_SVC_ID_RSVD_CTRL,
        PIPEDIR_IN,  /* in = DL = target -> host */
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
        PIPEDIR_IN,  /* in = DL = target -> host */
        1,
    },
    {
        ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
        PIPEDIR_OUT, /* out = UL = host -> target */
        4,
    },
    {
        ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
        PIPEDIR_IN,  /* in = DL = target -> host */
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

static void __ath10k_pci_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ASSERT_MTX_HELD(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps wake reg refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
            PCIE_SOC_WAKE_V_MASK);
}

static void __ath10k_pci_sleep(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ASSERT_MTX_HELD(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps sleep reg refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
            PCIE_SOC_WAKE_RESET);
    ar_pci->ps_awake = false;
}

static zx_status_t ath10k_pci_wake_wait(struct ath10k* ar) {
    int tot_delay = 0;
    int curr_delay = 5;

    while (tot_delay < PCIE_WAKE_TIMEOUT) {
        if (ath10k_pci_is_awake(ar)) {
            if (tot_delay > PCIE_WAKE_LATE_US)
                ath10k_warn("device wakeup took %d ms which is unusually long, otherwise it works normally.\n",
                            tot_delay / 1000);
            return ZX_OK;
        }

        zx_nanosleep(zx_deadline_after(ZX_USEC(curr_delay)));
        tot_delay += curr_delay;

        if (curr_delay < 50) {
            curr_delay += 5;
        }
    }

    return ZX_ERR_TIMED_OUT;
}

static zx_status_t ath10k_pci_force_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;

    if (ar_pci->pci_ps) {
        return ret;
    }

    mtx_lock(&ar_pci->ps_lock);

    if (!ar_pci->ps_awake) {
        WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
                PCIE_SOC_WAKE_V_MASK);

        ret = ath10k_pci_wake_wait(ar);
        if (ret == ZX_OK) {
            ar_pci->ps_awake = true;
        }
    }

    mtx_unlock(&ar_pci->ps_lock);

    return ret;
}

static void ath10k_pci_force_sleep(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    mtx_lock(&ar_pci->ps_lock);

    WRITE32(ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS + PCIE_SOC_WAKE_ADDRESS,
            PCIE_SOC_WAKE_RESET);
    ar_pci->ps_awake = false;

    mtx_unlock(&ar_pci->ps_lock);
}

static zx_status_t ath10k_pci_wake(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;

    if (ar_pci->pci_ps == 0) {
        return ret;
    }

    mtx_lock(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps wake refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    /* This function can be called very frequently. To avoid excessive
     * CPU stalls for MMIO reads use a cache var to hold the device state.
     */
    if (!ar_pci->ps_awake) {
        __ath10k_pci_wake(ar);

        ret = ath10k_pci_wake_wait(ar);
        if (ret == ZX_OK) {
            ar_pci->ps_awake = true;
        }
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

    if (ar_pci->pci_ps == 0) {
        return;
    }

    mtx_lock(&ar_pci->ps_lock);

    ath10k_dbg(ar, ATH10K_DBG_PCI_PS, "pci ps sleep refcount %lu awake %d\n",
               ar_pci->ps_wake_refcount, ar_pci->ps_awake);

    if (COND_WARN(ar_pci->ps_wake_refcount == 0)) {
        goto skip;
    }

    ar_pci->ps_wake_refcount--;

    mod_timer(&ar_pci->ps_timer, jiffies +
              msecs_to_jiffies(ATH10K_PCI_SLEEP_GRACE_PERIOD_MSEC));

skip:
    mtx_unlock(&ar_pci->ps_lock);
}

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
        ath10k_warn("failed to wake target for write32 of 0x%08x at 0x%08x: %s\n",
                    value, offset, zx_status_get_string(ret));
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
        ath10k_warn("failed to wake target for read32 at 0x%08x: %s\n",
                    offset, zx_status_get_string(ret));
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
    cause = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                              PCIE_INTR_CAUSE_ADDRESS);
    if (cause & (PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL)) {
        return true;
    }

    return false;
}

void ath10k_pci_disable_and_clear_legacy_irq(struct ath10k* ar) {
    /* IMPORTANT: INTR_CLR register has to be set after
     * INTR_ENABLE is set to 0, otherwise interrupt can not be
     * really cleared.
     */
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                       0);
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_CLR_ADDRESS,
                       PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

    /* IMPORTANT: this extra read transaction is required to
     * flush the posted write buffer.
     */
    (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                            PCIE_INTR_ENABLE_ADDRESS);
}

void ath10k_pci_enable_legacy_irq(struct ath10k* ar) {
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
                       PCIE_INTR_ENABLE_ADDRESS,
                       PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

    /* IMPORTANT: this extra read transaction is required to
     * flush the posted write buffer.
     */
    (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                            PCIE_INTR_ENABLE_ADDRESS);
}

static inline const char* ath10k_pci_get_irq_method(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_MSI) {
        return "msi";
    }

    return "legacy";
}

static zx_status_t __ath10k_pci_rx_post_buf(struct ath10k_pci_pipe* pipe) {
    struct ath10k* ar = pipe->hif_ce_state;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_ce_pipe* ce_pipe = pipe->ce_hdl;
    struct sk_buff* skb;
    dma_addr_t paddr;
    zx_status_t ret;

    skb = dev_alloc_skb(pipe->buf_sz);
    if (!skb) {
        return ZX_ERR_NO_MEMORY;
    }

    WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

    paddr = dma_map_single(ar->dev, skb->data,
                           skb->len + skb_tailroom(skb),
                           DMA_FROM_DEVICE);
    if (unlikely(dma_mapping_error(ar->dev, paddr))) {
        ath10k_warn("failed to dma map pci rx buf\n");
        dev_kfree_skb_any(skb);
        return ZX_ERR_IO;
    }

    ATH10K_SKB_RXCB(skb)->paddr = paddr;

    mtx_lock(&ar_pci->ce_lock);
    ret = __ath10k_ce_rx_post_buf(ce_pipe, skb, paddr);
    mtx_unlock(&ar_pci->ce_lock);
    if (ret != ZX_OK) {
        dma_unmap_single(ar->dev, paddr, skb->len + skb_tailroom(skb),
                         DMA_FROM_DEVICE);
        dev_kfree_skb_any(skb);
        return ret;
    }

    return ZX_OK;
}

static void ath10k_pci_rx_post_pipe(struct ath10k_pci_pipe* pipe) {
    struct ath10k* ar = pipe->hif_ce_state;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_ce_pipe* ce_pipe = pipe->ce_hdl;
    zx_status_t ret;
    int num;

    if (pipe->buf_sz == 0) {
        return;
    }

    if (!ce_pipe->dest_ring) {
        return;
    }

    mtx_lock(&ar_pci->ce_lock);
    num = __ath10k_ce_rx_num_free_bufs(ce_pipe);
    mtx_unlock(&ar_pci->ce_lock);

    while (num >= 0) {
        ret = __ath10k_pci_rx_post_buf(pipe);
        if (ret != ZX_OK) {
            if (ret == ZX_ERR_NO_SPACE) {
                break;
            }
            ath10k_warn("failed to post pci rx buf: %d\n", ret);
            mod_timer(&ar_pci->rx_post_retry, jiffies +
                      ATH10K_PCI_RX_POST_RETRY_MS);
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

void ath10k_pci_rx_replenish_retry(unsigned long ptr) {
    struct ath10k* ar = (void*)ptr;

    ath10k_pci_rx_post(ar);
}

static zx_status_t ath10k_pci_qca988x_targ_cpu_to_ce_addr(struct ath10k* ar, uint32_t addr,
                                                          uint32_t* ce_addr) {
    uint32_t val = 0, region = addr & 0xfffff;

    val = (ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS)
           & 0x7ff) << 21;
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

    if (COND_WARN_ONCE(!ar_pci->targ_cpu_to_ce_addr)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

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
    dma_addr_t ce_data_base = 0;
    void* data_buf = NULL;
    int i;

    mtx_lock(&ar_pci->ce_lock);

    ce_diag = ar_pci->ce_diag;

    /*
     * Allocate a temporary bounce buffer to hold caller's data
     * to be DMA'ed from Target. This guarantees
     *   1) 4-byte alignment
     *   2) Buffer in DMA-able space
     */
    alloc_nbytes = MIN_T(unsigned int, nbytes, DIAG_TRANSFER_LIMIT);

    data_buf = (unsigned char*)dma_zalloc_coherent(ar->dev,
               alloc_nbytes,
               &ce_data_base,
               GFP_ATOMIC);

    if (!data_buf) {
        ret = ZX_ERR_NO_MEMORY;
        goto done;
    }

    remaining_bytes = nbytes;
    ce_data = ce_data_base;
    while (remaining_bytes) {
        nbytes = MIN_T(unsigned int, remaining_bytes,
                       DIAG_TRANSFER_LIMIT);

        ret = __ath10k_ce_rx_post_buf(ce_diag, &ce_data, ce_data);
        if (ret != ZX_OK) {
            goto done;
        }

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
        if (ret != ZX_OK) {
            goto done;
        }

        ret = ath10k_ce_send_nolock(ce_diag, NULL, ce_address, nbytes, 0, 0);
        if (ret != ZX_OK) {
            goto done;
        }

        i = 0;
        while (ath10k_ce_completed_send_next_nolock(ce_diag,
                NULL) != ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        i = 0;
        while (ath10k_ce_completed_recv_next_nolock(ce_diag,
                (void**)&buf,
                &completed_nbytes)
                != ZX_OK) {
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

    if (data_buf)
        dma_free_coherent(ar->dev, alloc_nbytes, data_buf,
                          ce_data_base);

    mtx_unlock(&ar_pci->ce_lock);

    return ret;
}

static zx_status_t ath10k_pci_diag_read32(struct ath10k* ar, uint32_t address, uint32_t* value) {
    return ath10k_pci_diag_read_mem(ar, address, value, sizeof(*value));
}

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

#define ath10k_pci_diag_read_hi(ar, dest, src, len)     \
    __ath10k_pci_diag_read_hi(ar, dest, HI_ITEM(src), len)

zx_status_t ath10k_pci_diag_write_mem(struct ath10k* ar, uint32_t address,
                                      const void* data, int nbytes) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret = ZX_OK;
    uint32_t* buf;
    unsigned int completed_nbytes, orig_nbytes, remaining_bytes;
    struct ath10k_ce_pipe* ce_diag;
    void* data_buf = NULL;
    uint32_t ce_data;    /* Host buffer address in CE space */
    dma_addr_t ce_data_base = 0;
    int i;

    mtx_lock(&ar_pci->ce_lock);

    ce_diag = ar_pci->ce_diag;

    /*
     * Allocate a temporary bounce buffer to hold caller's data
     * to be DMA'ed to Target. This guarantees
     *   1) 4-byte alignment
     *   2) Buffer in DMA-able space
     */
    orig_nbytes = nbytes;
    data_buf = (unsigned char*)dma_alloc_coherent(ar->dev,
               orig_nbytes,
               &ce_data_base,
               GFP_ATOMIC);
    if (!data_buf) {
        ret = ZX_ERR_NO_MEMORY;
        goto done;
    }

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
    if (ret != ZX_OK) {
        goto done;
    }

    remaining_bytes = orig_nbytes;
    ce_data = ce_data_base;
    while (remaining_bytes) {
        /* FIXME: check cast */
        nbytes = MIN_T(int, remaining_bytes, DIAG_TRANSFER_LIMIT);

        /* Set up to receive directly into Target(!) address */
        ret = __ath10k_ce_rx_post_buf(ce_diag, &ce_address, ce_address);
        if (ret != ZX_OK) {
            goto done;
        }

        /*
         * Request CE to send caller-supplied data that
         * was copied to bounce buffer to Target(!) address.
         */
        ret = ath10k_ce_send_nolock(ce_diag, NULL, (uint32_t)ce_data,
                                    nbytes, 0, 0);
        if (ret != ZX_OK) {
            goto done;
        }

        i = 0;
        while (ath10k_ce_completed_send_next_nolock(ce_diag,
                NULL) != ZX_OK) {
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

            if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                ret = ZX_ERR_SHOULD_WAIT;
                goto done;
            }
        }

        i = 0;
        while (ath10k_ce_completed_recv_next_nolock(ce_diag,
                (void**)&buf,
                &completed_nbytes)
                != 0) {
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
    if (data_buf) {
        dma_free_coherent(ar->dev, orig_nbytes, data_buf,
                          ce_data_base);
    }

    if (ret != ZX_OK) {
        ath10k_warn("failed to write diag value at 0x%x: %s\n",
                    address, zx_status_get_string(ret));
    }

    mtx_unlock(&ar_pci->ce_lock);

    return ret;
}

static zx_status_t ath10k_pci_diag_write32(struct ath10k* ar, uint32_t address, uint32_t value) {
    uint32_t val = value;

    return ath10k_pci_diag_write_mem(ar, address, &val, sizeof(val));
}

/* Called by lower (CE) layer when a send to Target completes. */
static void ath10k_pci_htc_tx_cb(struct ath10k_ce_pipe* ce_state) {
    struct ath10k* ar = ce_state->ar;
    struct sk_buff_head list;
    struct sk_buff* skb;

    __skb_queue_head_init(&list);
    while (ath10k_ce_completed_send_next(ce_state, (void**)&skb) == 0) {
        /* no need to call tx completion for NULL pointers */
        if (skb == NULL) {
            continue;
        }

        __skb_queue_tail(&list, skb);
    }

    while ((skb = __skb_dequeue(&list))) {
        ath10k_htc_tx_completion_handler(ar, skb);
    }
}

static void ath10k_pci_process_rx_cb(struct ath10k_ce_pipe* ce_state,
                                     void (*callback)(struct ath10k* ar,
                                             struct sk_buff* skb)) {
    struct ath10k* ar = ce_state->ar;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pipe_info =  &ar_pci->pipe_info[ce_state->id];
    struct sk_buff* skb;
    struct sk_buff_head list;
    void* transfer_context;
    unsigned int nbytes, max_nbytes;

    __skb_queue_head_init(&list);
    while (ath10k_ce_completed_recv_next(ce_state, &transfer_context,
                                         &nbytes) == 0) {
        skb = transfer_context;
        max_nbytes = skb->len + skb_tailroom(skb);
        dma_unmap_single(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
                         max_nbytes, DMA_FROM_DEVICE);

        if (unlikely(max_nbytes < nbytes)) {
            ath10k_warn("rxed more than expected (nbytes %d, max %d)",
                        nbytes, max_nbytes);
            dev_kfree_skb_any(skb);
            continue;
        }

        skb_put(skb, nbytes);
        __skb_queue_tail(&list, skb);
    }

    while ((skb = __skb_dequeue(&list))) {
        ath10k_dbg(ar, ATH10K_DBG_PCI, "pci rx ce pipe %d len %d\n",
                   ce_state->id, skb->len);
        ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci rx: ",
                        skb->data, skb->len);

        callback(ar, skb);
    }

    ath10k_pci_rx_post_pipe(pipe_info);
}

static void ath10k_pci_process_htt_rx_cb(struct ath10k_ce_pipe* ce_state,
        void (*callback)(struct ath10k* ar,
                         struct sk_buff* skb)) {
    struct ath10k* ar = ce_state->ar;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pipe_info =  &ar_pci->pipe_info[ce_state->id];
    struct ath10k_ce_pipe* ce_pipe = pipe_info->ce_hdl;
    struct sk_buff* skb;
    struct sk_buff_head list;
    void* transfer_context;
    unsigned int nbytes, max_nbytes, nentries;
    int orig_len;

    /* No need to aquire ce_lock for CE5, since this is the only place CE5
     * is processed other than init and deinit. Before releasing CE5
     * buffers, interrupts are disabled. Thus CE5 access is serialized.
     */
    __skb_queue_head_init(&list);
    while (ath10k_ce_completed_recv_next_nolock(ce_state, &transfer_context,
            &nbytes) == 0) {
        skb = transfer_context;
        max_nbytes = skb->len + skb_tailroom(skb);

        if (unlikely(max_nbytes < nbytes)) {
            ath10k_warn("rxed more than expected (nbytes %d, max %d)",
                        nbytes, max_nbytes);
            continue;
        }

        dma_sync_single_for_cpu(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
                                max_nbytes, DMA_FROM_DEVICE);
        skb_put(skb, nbytes);
        __skb_queue_tail(&list, skb);
    }

    nentries = skb_queue_len(&list);
    while ((skb = __skb_dequeue(&list))) {
        ath10k_dbg(ar, ATH10K_DBG_PCI, "pci rx ce pipe %d len %d\n",
                   ce_state->id, skb->len);
        ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci rx: ",
                        skb->data, skb->len);

        orig_len = skb->len;
        callback(ar, skb);
        skb_push(skb, orig_len - skb->len);
        skb_reset_tail_pointer(skb);
        skb_trim(skb, 0);

        /*let device gain the buffer again*/
        dma_sync_single_for_device(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
                                   skb->len + skb_tailroom(skb),
                                   DMA_FROM_DEVICE);
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
    ath10k_pci_process_rx_cb(ce_state,
                             ath10k_htt_rx_pktlog_completion_handler);
}

/* Called by lower (CE) layer when a send to HTT Target completes. */
static void ath10k_pci_htt_tx_cb(struct ath10k_ce_pipe* ce_state) {
    struct ath10k* ar = ce_state->ar;
    struct sk_buff* skb;

    while (ath10k_ce_completed_send_next(ce_state, (void**)&skb) == ZX_OK) {
        /* no need to call tx completion for NULL pointers */
        if (!skb) {
            continue;
        }

        dma_unmap_single(ar->dev, ATH10K_SKB_CB(skb)->paddr,
                         skb->len, DMA_TO_DEVICE);
        ath10k_htt_hif_tx_complete(ar, skb);
    }
}

static void ath10k_pci_htt_rx_deliver(struct ath10k* ar, struct sk_buff* skb) {
    skb_pull(skb, sizeof(struct ath10k_htc_hdr));
    ath10k_htt_t2h_msg_handler(ar, skb);
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

    if (unlikely(CE_RING_DELTA(nentries_mask,
                               write_index, sw_index - 1) < (unsigned)n_items)) {
        err = ZX_ERR_NO_RESOURCES;
        goto err;
    }

    for (i = 0; i < n_items - 1; i++) {
        ath10k_dbg(ar, ATH10K_DBG_PCI,
                   "pci tx item %d paddr 0x%08x len %d n_items %d\n",
                   i, items[i].paddr, items[i].len, n_items);
        ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci tx data: ",
                        items[i].vaddr, items[i].len);

        err = ath10k_ce_send_nolock(ce_pipe,
                                    items[i].transfer_context,
                                    items[i].paddr,
                                    items[i].len,
                                    items[i].transfer_id,
                                    CE_SEND_FLAG_GATHER);
        if (err != ZX_OK) {
            goto err;
        }
    }

    /* `i` is equal to `n_items -1` after for() */

    ath10k_dbg(ar, ATH10K_DBG_PCI,
               "pci tx item %d paddr 0x%08x len %d n_items %d\n",
               i, items[i].paddr, items[i].len, n_items);
    ath10k_dbg_dump(ar, ATH10K_DBG_PCI_DUMP, NULL, "pci tx data: ",
                    items[i].vaddr, items[i].len);

    err = ath10k_ce_send_nolock(ce_pipe,
                                items[i].transfer_context,
                                items[i].paddr,
                                items[i].len,
                                items[i].transfer_id,
                                0);
    if (err) {
        goto err;
    }

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

static void ath10k_pci_fw_crashed_dump(struct ath10k* ar) {
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
}

void ath10k_pci_hif_send_complete_check(struct ath10k* ar, uint8_t pipe,
                                        int force) {
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
        if ((unsigned)resources > (host_ce_config_wlan[pipe].src_nentries >> 1)) {
            return;
        }
    }
    ath10k_ce_per_engine_service(ar, pipe);
}

static void ath10k_pci_rx_retry_sync(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    del_timer_sync(&ar_pci->rx_post_retry);
}

zx_status_t ath10k_pci_hif_map_service_to_pipe(struct ath10k* ar, uint16_t service_id,
                                               uint8_t* ul_pipe, uint8_t* dl_pipe) {
    const struct service_to_pipe* entry;
    bool ul_set = false, dl_set = false;
    unsigned int i;

    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif map service\n");

    for (i = 0; i < countof(target_service_to_ce_map_wlan); i++) {
        entry = &target_service_to_ce_map_wlan[i];

        if (entry->service_id != service_id) {
            continue;
        }

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

    if (COND_WARN(!ul_set || !dl_set)) {
        return ZX_ERR_NOT_FOUND;
    }

    return ZX_OK;
}

void ath10k_pci_hif_get_default_pipe(struct ath10k* ar,
                                     uint8_t* ul_pipe, uint8_t* dl_pipe) {
    ath10k_dbg(ar, ATH10K_DBG_PCI, "pci hif get default pipe\n");

    (void)ath10k_pci_hif_map_service_to_pipe(ar,
            ATH10K_HTC_SVC_ID_RSVD_CTRL,
            ul_pipe, dl_pipe);
}

void ath10k_pci_irq_msi_fw_mask(struct ath10k* ar) {
    uint32_t val;

    switch (ar->hw_rev) {
    case ATH10K_HW_QCA988X:
    case ATH10K_HW_QCA9887:
    case ATH10K_HW_QCA6174:
    case ATH10K_HW_QCA9377:
        val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                                CORE_CTRL_ADDRESS);
        val &= ~CORE_CTRL_PCIE_REG_31_MASK;
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
                           CORE_CTRL_ADDRESS, val);
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
        val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                                CORE_CTRL_ADDRESS);
        val |= CORE_CTRL_PCIE_REG_31_MASK;
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
                           CORE_CTRL_ADDRESS, val);
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
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    synchronize_irq(ar_pci->pdev->irq);
}

static void ath10k_pci_irq_enable(struct ath10k* ar) {
    ath10k_ce_enable_interrupts(ar);
    ath10k_pci_enable_legacy_irq(ar);
    ath10k_pci_irq_msi_fw_unmask(ar);
}

static zx_status_t ath10k_pci_hif_start(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif start\n");

    napi_enable(&ar->napi);

    ath10k_pci_irq_enable(ar);
    ath10k_pci_rx_post(ar);

    pcie_capability_write_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                               ar_pci->link_ctl);

    return ZX_OK;
}

static void ath10k_pci_rx_pipe_cleanup(struct ath10k_pci_pipe* pci_pipe) {
    struct ath10k* ar;
    struct ath10k_ce_pipe* ce_pipe;
    struct ath10k_ce_ring* ce_ring;
    struct sk_buff* skb;
    int i;

    ar = pci_pipe->hif_ce_state;
    ce_pipe = pci_pipe->ce_hdl;
    ce_ring = ce_pipe->dest_ring;

    if (!ce_ring) {
        return;
    }

    if (!pci_pipe->buf_sz) {
        return;
    }

    for (i = 0; i < ce_ring->nentries; i++) {
        skb = ce_ring->per_transfer_context[i];
        if (!skb) {
            continue;
        }

        ce_ring->per_transfer_context[i] = NULL;

        dma_unmap_single(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
                         skb->len + skb_tailroom(skb),
                         DMA_FROM_DEVICE);
        dev_kfree_skb_any(skb);
    }
}

static void ath10k_pci_tx_pipe_cleanup(struct ath10k_pci_pipe* pci_pipe) {
    struct ath10k* ar;
    struct ath10k_ce_pipe* ce_pipe;
    struct ath10k_ce_ring* ce_ring;
    struct sk_buff* skb;
    int i;

    ar = pci_pipe->hif_ce_state;
    ce_pipe = pci_pipe->ce_hdl;
    ce_ring = ce_pipe->src_ring;

    if (!ce_ring) {
        return;
    }

    if (!pci_pipe->buf_sz) {
        return;
    }

    for (i = 0; i < ce_ring->nentries; i++) {
        skb = ce_ring->per_transfer_context[i];
        if (!skb) {
            continue;
        }

        ce_ring->per_transfer_context[i] = NULL;

        ath10k_htc_tx_completion_handler(ar, skb);
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
    ath10k_pci_rx_retry_sync(ar);
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
    napi_synchronize(&ar->napi);
    napi_disable(&ar->napi);

    mtx_lock(&ar_pci->ps_lock);
    COND_WARN(ar_pci->ps_wake_refcount > 0);
    mtx_unlock(&ar_pci->ps_lock);
}

zx_status_t ath10k_pci_hif_exchange_bmi_msg(struct ath10k* ar,
                                            void* req, uint32_t req_len,
                                            void* resp, uint32_t* resp_len) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct ath10k_pci_pipe* pci_tx = &ar_pci->pipe_info[BMI_CE_NUM_TO_TARG];
    struct ath10k_pci_pipe* pci_rx = &ar_pci->pipe_info[BMI_CE_NUM_TO_HOST];
    struct ath10k_ce_pipe* ce_tx = pci_tx->ce_hdl;
    struct ath10k_ce_pipe* ce_rx = pci_rx->ce_hdl;
    dma_addr_t req_paddr = 0;
    dma_addr_t resp_paddr = 0;
    struct bmi_xfer xfer = {};
    void* treq, *tresp = NULL;
    zx_status_t ret = ZX_OK;

    might_sleep();

    if (resp && !resp_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (resp && resp_len && *resp_len == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    treq = kmemdup(req, req_len, GFP_KERNEL);
    if (!treq) {
        return ZX_ERR_NO_MEMORY;
    }

    req_paddr = dma_map_single(ar->dev, treq, req_len, DMA_TO_DEVICE);
    ret = dma_mapping_error(ar->dev, req_paddr);
    if (ret != ZX_OK) {
        ret = ZX_ERR_IO;
        goto err_dma;
    }

    if (resp && resp_len) {
        tresp = kzalloc(*resp_len, GFP_KERNEL);
        if (!tresp) {
            ret = ZX_ERR_NO_MEMORY;
            goto err_req;
        }

        resp_paddr = dma_map_single(ar->dev, tresp, *resp_len,
                                    DMA_FROM_DEVICE);
        ret = dma_mapping_error(ar->dev, resp_paddr);
        if (ret) {
            ret = ZX_ERR_IO;
            goto err_req;
        }

        xfer.wait_for_resp = true;
        xfer.resp_len = 0;

        ath10k_ce_rx_post_buf(ce_rx, &xfer, resp_paddr);
    }

    ret = ath10k_ce_send(ce_tx, &xfer, req_paddr, req_len, -1, 0);
    if (ret != ZX_OK) {
        goto err_resp;
    }

    ret = ath10k_pci_bmi_wait(ar, ce_tx, ce_rx, &xfer);
    if (ret != ZX_OK) {
        uint32_t unused_buffer;
        unsigned int unused_nbytes;
        unsigned int unused_id;

        ath10k_ce_cancel_send_next(ce_tx, NULL, &unused_buffer,
                                   &unused_nbytes, &unused_id);
    } else {
        /* non-zero means we did not time out */
        ret = ZX_OK;
    }

err_resp:
    if (resp) {
        uint32_t unused_buffer;

        ath10k_ce_revoke_recv_next(ce_rx, NULL, &unused_buffer);
        dma_unmap_single(ar->dev, resp_paddr,
                         *resp_len, DMA_FROM_DEVICE);
    }
err_req:
    dma_unmap_single(ar->dev, req_paddr, req_len, DMA_TO_DEVICE);

    if (ret == ZX_OK && resp_len) {
        *resp_len = MIN(*resp_len, xfer.resp_len);
        memcpy(resp, tresp, xfer.resp_len);
    }
err_dma:
    kfree(treq);
    kfree(tresp);

    return ret;
}

static void ath10k_pci_bmi_send_done(struct ath10k_ce_pipe* ce_state) {
    struct bmi_xfer* xfer;

    if (ath10k_ce_completed_send_next(ce_state, (void**)&xfer)) {
        return;
    }

    xfer->tx_done = true;
}

static void ath10k_pci_bmi_recv_data(struct ath10k_ce_pipe* ce_state) {
    struct ath10k* ar = ce_state->ar;
    struct bmi_xfer* xfer;
    unsigned int nbytes;

    if (ath10k_ce_completed_recv_next(ce_state, (void**)&xfer,
                                      &nbytes)) {
        return;
    }

    if (COND_WARN_ONCE(!xfer)) {
        return;
    }

    if (!xfer->wait_for_resp) {
        ath10k_warn("unexpected: BMI data received; ignoring\n");
        return;
    }

    xfer->resp_len = nbytes;
    xfer->rx_done = true;
}

static zx_status_t ath10k_pci_bmi_wait(struct ath10k* ar,
                                       struct ath10k_ce_pipe* tx_pipe,
                                       struct ath10k_ce_pipe* rx_pipe,
                                       struct bmi_xfer* xfer) {
    unsigned long timeout = jiffies + BMI_COMMUNICATION_TIMEOUT_HZ;
    unsigned long started = jiffies;
    unsigned long dur;
    zx_status_t ret;

    while (time_before_eq(jiffies, timeout)) {
        ath10k_pci_bmi_send_done(tx_pipe);
        ath10k_pci_bmi_recv_data(rx_pipe);

        if (xfer->tx_done && (xfer->rx_done == xfer->wait_for_resp)) {
            ret = ZX_OK;
            goto out;
        }

        schedule();
    }

    ret = ZX_ERR_TIMED_OUT;

out:
    dur = jiffies - started;
    if (dur > HZ)
        ath10k_dbg(ar, ATH10K_DBG_BMI,
                   "bmi cmd took %lu jiffies hz %d ret %d\n",
                   dur, HZ, ret);
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
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    switch (ar_pci->pdev->device) {
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
    interconnect_targ_addr =
        host_interest_item_address(HI_ITEM(hi_interconnect_state));

    /* Supply Target-side CE configuration */
    ret = ath10k_pci_diag_read32(ar, interconnect_targ_addr,
                                 &pcie_state_targ_addr);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get pcie state addr: %s\n", zx_status_get_string(ret));
        return ret;
    }

    if (pcie_state_targ_addr == 0) {
        ret = ZX_ERR_IO;
        ath10k_err("Invalid pcie state addr\n");
        return ret;
    }

    ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
                                      offsetof(struct pcie_state,
                                              pipe_cfg_addr)),
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

    ret = ath10k_pci_diag_write_mem(ar, pipe_cfg_targ_addr,
                                    target_ce_config_wlan,
                                    sizeof(struct ce_pipe_config) *
                                    NUM_TARGET_CE_CONFIG_WLAN);

    if (ret != ZX_OK) {
        ath10k_err("Failed to write pipe cfg: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
                                      offsetof(struct pcie_state,
                                              svc_to_pipe_map)),
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

    ret = ath10k_pci_diag_write_mem(ar, svc_to_pipe_map,
                                    target_service_to_ce_map_wlan,
                                    sizeof(target_service_to_ce_map_wlan));
    if (ret != ZX_OK) {
        ath10k_err("Failed to write svc/pipe map: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
                                      offsetof(struct pcie_state,
                                              config_flags)),
                                 &pcie_config_flags);
    if (ret != ZX_OK) {
        ath10k_err("Failed to get pcie config_flags: %s\n", zx_status_get_string(ret));
        return ret;
    }

    pcie_config_flags &= ~PCIE_CONFIG_FLAG_ENABLE_L1;

    ret = ath10k_pci_diag_write32(ar, (pcie_state_targ_addr +
                                       offsetof(struct pcie_state,
                                               config_flags)),
                                  pcie_config_flags);
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
    ealloc_value |= ((HI_EARLY_ALLOC_MAGIC << HI_EARLY_ALLOC_MAGIC_SHIFT) &
                     HI_EARLY_ALLOC_MAGIC_MASK);
    ealloc_value |= ((ath10k_bus_get_num_banks(ar) <<
                      HI_EARLY_ALLOC_IRAM_BANKS_SHIFT) &
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
            ath10k_err("failed to allocate copy engine pipe %d: %s\n",
                       i, zx_status_get_string(ret));
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
            ath10k_err("failed to initialize copy engine pipe %d: %s\n",
                       i, zx_status_get_string(ret));
            return ret;
        }
    }

    return ZX_OK;
}

static bool ath10k_pci_has_fw_crashed(struct ath10k* ar) {
    return ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS) &
           FW_IND_EVENT_PENDING;
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
    ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
                           val | SOC_RESET_CONTROL_SI0_RST_MASK);
    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
    ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
                           val & ~SOC_RESET_CONTROL_SI0_RST_MASK);
    val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
}

static void ath10k_pci_warm_reset_cpu(struct ath10k* ar) {
    uint32_t val;

    ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, 0);

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                            SOC_RESET_CONTROL_ADDRESS);
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val | SOC_RESET_CONTROL_CPU_WARM_RST_MASK);
}

static void ath10k_pci_warm_reset_ce(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                            SOC_RESET_CONTROL_ADDRESS);

    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val | SOC_RESET_CONTROL_CE_RST_MASK);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                       val & ~SOC_RESET_CONTROL_CE_RST_MASK);
}

static void ath10k_pci_warm_reset_clear_lf(struct ath10k* ar) {
    uint32_t val;

    val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                            SOC_LF_TIMER_CONTROL0_ADDRESS);
    ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS +
                       SOC_LF_TIMER_CONTROL0_ADDRESS,
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

    if (!ar_pci->pci_soft_reset) {
        return ZX_ERR_NOT_SUPPORTED;
    }

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
            ath10k_warn("failed to warm reset attempt %d of %d: %s\n",
                        i + 1, ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS,
                        zx_status_get_string(ret));
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
            ath10k_warn("failed to init copy engine: %s\n",
                        zx_status_get_string(ret));
            continue;
        }

        ret = ath10k_pci_diag_read32(ar, QCA988X_HOST_INTEREST_ADDRESS,
                                     &val);
        if (ret != ZX_OK) {
            ath10k_warn("failed to poke copy engine: %s\n",
                        zx_status_get_string(ret));
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
        ath10k_warn("failed to wait for target after cold reset: %s\n",
                    zx_status_get_string(ret));
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
        ath10k_warn("failed to wait for target after cold reset: %s\n",
                    zx_status_get_string(ret));
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
        ath10k_warn("failed to wait for target after cold reset: %s\n",
                    zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca99x0 chip reset complete (cold)\n");

    return ZX_OK;
}

static zx_status_t ath10k_pci_chip_reset(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    if (COND_WARN(!ar_pci->pci_hard_reset)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar_pci->pci_hard_reset(ar);
}

static zx_status_t ath10k_pci_hif_power_up(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power up\n");

    pcie_capability_read_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                              &ar_pci->link_ctl);
    pcie_capability_write_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                               ar_pci->link_ctl & ~PCI_EXP_LNKCTL_ASPMC);

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
    if ((val & 0x0000ff00) != 0) {
        pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);
    }

    return ret;
}
#endif

static bool ath10k_pci_validate_cal(void* data, size_t size) {
    uint16_t* cal_words = data;
    uint16_t checksum = 0;
    size_t i;

    if (size % 2 != 0) {
        return false;
    }

    for (i = 0; i < size / 2; i++) {
        checksum ^= cal_words[i];
    }

    return checksum == 0xffff;
}

static void ath10k_pci_enable_eeprom(struct ath10k* ar) {
    /* Enable SI clock */
    ath10k_pci_soc_write32(ar, CLOCK_CONTROL_OFFSET, 0x0);

    /* Configure GPIOs for I2C operation */
    ath10k_pci_write32(ar,
                       GPIO_BASE_ADDRESS + GPIO_PIN0_OFFSET +
                       4 * QCA9887_1_0_I2C_SDA_GPIO_PIN,
                       SM(QCA9887_1_0_I2C_SDA_PIN_CONFIG,
                          GPIO_PIN0_CONFIG) |
                       SM(1, GPIO_PIN0_PAD_PULL));

    ath10k_pci_write32(ar,
                       GPIO_BASE_ADDRESS + GPIO_PIN0_OFFSET +
                       4 * QCA9887_1_0_SI_CLK_GPIO_PIN,
                       SM(QCA9887_1_0_SI_CLK_PIN_CONFIG, GPIO_PIN0_CONFIG) |
                       SM(1, GPIO_PIN0_PAD_PULL));

    ath10k_pci_write32(ar,
                       GPIO_BASE_ADDRESS +
                       QCA9887_1_0_GPIO_ENABLE_W1TS_LOW_ADDRESS,
                       1u << QCA9887_1_0_SI_CLK_GPIO_PIN);

    /* In Swift ASIC - EEPROM clock will be (110MHz/512) = 214KHz */
    ath10k_pci_write32(ar,
                       SI_BASE_ADDRESS + SI_CONFIG_OFFSET,
                       SM(1, SI_CONFIG_ERR_INT) |
                       SM(1, SI_CONFIG_BIDIR_OD_DATA) |
                       SM(1, SI_CONFIG_I2C) |
                       SM(1, SI_CONFIG_POS_SAMPLE) |
                       SM(1, SI_CONFIG_INACTIVE_DATA) |
                       SM(1, SI_CONFIG_INACTIVE_CLK) |
                       SM(8, SI_CONFIG_DIVIDER));
}

static zx_status_t ath10k_pci_read_eeprom(struct ath10k* ar, uint16_t addr, uint8_t* out) {
    uint32_t reg;
    int wait_limit;

    /* set device select byte and for the read operation */
    reg = QCA9887_EEPROM_SELECT_READ |
          SM(addr, QCA9887_EEPROM_ADDR_LO) |
          SM(addr >> 8, QCA9887_EEPROM_ADDR_HI);
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_TX_DATA0_OFFSET, reg);

    /* write transmit data, transfer length, and START bit */
    ath10k_pci_write32(ar, SI_BASE_ADDRESS + SI_CS_OFFSET,
                       SM(1, SI_CS_START) | SM(1, SI_CS_RX_CNT) |
                       SM(4, SI_CS_TX_CNT));

    /* wait max 1 sec */
    wait_limit = 100000;

    /* wait for SI_CS_DONE_INT */
    do {
        reg = ath10k_pci_read32(ar, SI_BASE_ADDRESS + SI_CS_OFFSET);
        if (MS(reg, SI_CS_DONE_INT)) {
            break;
        }

        wait_limit--;
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    } while (wait_limit > 0);

    if (!MS(reg, SI_CS_DONE_INT)) {
        ath10k_err("timeout while reading device EEPROM at %04x\n",
                   addr);
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

    if (!QCA_REV_9887(ar)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    calsize = ar->hw_params.cal_data_len;
    caldata = kmalloc(calsize, GFP_KERNEL);
    if (!caldata) {
        return ZX_ERR_NO_MEMORY;
    }

    ath10k_pci_enable_eeprom(ar);

    for (i = 0; i < calsize; i++) {
        ret = ath10k_pci_read_eeprom(ar, i, &caldata[i]);
        if (ret != ZX_OK) {
            goto err_free;
        }
    }

    if (!ath10k_pci_validate_cal(caldata, calsize)) {
        goto err_free;
    }

    *data = caldata;
    *data_len = calsize;

    return ZX_OK;

err_free:
    kfree(caldata);

    return ZX_ERR_INVALID_ARGS;
}

static const struct ath10k_hif_ops ath10k_pci_hif_ops = {
    .tx_sg                  = ath10k_pci_hif_tx_sg,
    .diag_read              = ath10k_pci_hif_diag_read,
    .diag_write             = ath10k_pci_diag_write_mem,
    .exchange_bmi_msg       = ath10k_pci_hif_exchange_bmi_msg,
    .start                  = ath10k_pci_hif_start,
    .stop                   = ath10k_pci_hif_stop,
    .map_service_to_pipe    = ath10k_pci_hif_map_service_to_pipe,
    .get_default_pipe       = ath10k_pci_hif_get_default_pipe,
    .send_complete_check    = ath10k_pci_hif_send_complete_check,
    .get_free_queue_number  = ath10k_pci_hif_get_free_queue_number,
    .power_up               = ath10k_pci_hif_power_up,
    .power_down             = ath10k_pci_hif_power_down,
    .read32                 = ath10k_pci_read32,
    .write32                = ath10k_pci_write32,
#ifdef CONFIG_PM
    .suspend                = ath10k_pci_hif_suspend,
    .resume                 = ath10k_pci_hif_resume,
#endif
    .fetch_cal_eeprom       = ath10k_pci_hif_fetch_cal_eeprom,
};

/*
 * Top-level interrupt handler for all PCI interrupts from a Target.
 * When a block of MSI interrupts is allocated, this top-level handler
 * is not used; instead, we directly call the correct sub-handler.
 */
static irqreturn_t ath10k_pci_interrupt_handler(int irq, void* arg) {
    struct ath10k* ar = arg;
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    if (ath10k_pci_has_device_gone(ar)) {
        return IRQ_NONE;
    }

    ret = ath10k_pci_force_wake(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to wake device up on irq: %s\n", zx_status_get_string(ret));
        return IRQ_NONE;
    }

    if ((ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_LEGACY) &&
            !ath10k_pci_irq_pending(ar)) {
        return IRQ_NONE;
    }

    ath10k_pci_disable_and_clear_legacy_irq(ar);
    ath10k_pci_irq_msi_fw_mask(ar);
    napi_schedule(&ar->napi);

    return IRQ_HANDLED;
}

static int ath10k_pci_napi_poll(struct napi_struct* ctx, int budget) {
    struct ath10k* ar = container_of(ctx, struct ath10k, napi);
    int done = 0;

    if (ath10k_pci_has_fw_crashed(ar)) {
        ath10k_pci_fw_crashed_clear(ar);
        ath10k_pci_fw_crashed_dump(ar);
        napi_complete(ctx);
        return done;
    }

    ath10k_ce_per_engine_service_any(ar);

    done = ath10k_htt_txrx_compl_task(ar, budget);

    if (done < budget) {
        napi_complete_done(ctx, done);
        /* In case of MSI, it is possible that interrupts are received
         * while NAPI poll is inprogress. So pending interrupts that are
         * received after processing all copy engine pipes by NAPI poll
         * will not be handled again. This is causing failure to
         * complete boot sequence in x86 platform. So before enabling
         * interrupts safer to check for pending interrupts for
         * immediate servicing.
         */
        if (CE_INTERRUPT_SUMMARY(ar)) {
            napi_reschedule(ctx);
            goto out;
        }
        ath10k_pci_enable_legacy_irq(ar);
        ath10k_pci_irq_msi_fw_unmask(ar);
    }

out:
    return done;
}

static zx_status_t ath10k_pci_request_irq_msi(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    ret = request_irq(ar_pci->pdev->irq,
                      ath10k_pci_interrupt_handler,
                      IRQF_SHARED, "ath10k_pci", ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to request MSI irq %d: %s\n",
                    ar_pci->pdev->irq, zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

static zx_status_t ath10k_pci_request_irq_legacy(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    ret = request_irq(ar_pci->pdev->irq,
                      ath10k_pci_interrupt_handler,
                      IRQF_SHARED, "ath10k_pci", ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to request legacy irq %d: %s\n",
                    ar_pci->pdev->irq, zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

static zx_status_t ath10k_pci_request_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    switch (ar_pci->oper_irq_mode) {
    case ATH10K_PCI_IRQ_LEGACY:
        return ath10k_pci_request_irq_legacy(ar);
    case ATH10K_PCI_IRQ_MSI:
        return ath10k_pci_request_irq_msi(ar);
    default:
        return ZX_ERR_BAD_STATE;
    }
}

static void ath10k_pci_free_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    free_irq(ar_pci->pdev->irq, ar);
}

void ath10k_pci_init_napi(struct ath10k* ar) {
    netif_napi_add(&ar->napi_dev, &ar->napi, ath10k_pci_napi_poll,
                   ATH10K_NAPI_BUDGET);
}

static zx_status_t ath10k_pci_init_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    ath10k_pci_init_napi(ar);

    if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_AUTO)
        ath10k_trace("limiting irq mode to: %d\n", ath10k_pci_irq_mode);

    /* Try MSI */
    if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_LEGACY) {
        ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_MSI;
        ret = pci_enable_msi(ar_pci->pdev);
        if (ret == ZX_OK) {
            return ZX_OK;
        }

        /* fall-through */
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
    ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_LEGACY;

    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                       PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

    return ZX_OK;
}

static void ath10k_pci_deinit_irq_legacy(struct ath10k* ar) {
    ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                       0);
}

static zx_status_t ath10k_pci_deinit_irq(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);

    switch (ar_pci->oper_irq_mode) {
    case ATH10K_PCI_IRQ_LEGACY:
        ath10k_pci_deinit_irq_legacy(ar);
        break;
    default:
        pci_disable_msi(ar_pci->pdev);
        break;
    }

    return ZX_OK;
}

zx_status_t ath10k_pci_wait_for_target_init(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    unsigned long timeout;
    uint32_t val;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot waiting target to initialise\n");

    timeout = jiffies + msecs_to_jiffies(ATH10K_PCI_TARGET_WAIT);

    do {
        val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target indicator %x\n",
                   val);

        /* target should never return this */
        if (val == 0xffffffff) {
            continue;
        }

        /* the device has crashed so don't bother trying anymore */
        if (val & FW_IND_EVENT_PENDING) {
            break;
        }

        if (val & FW_IND_INITIALIZED) {
            break;
        }

        if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_LEGACY)
            /* Fix potential race by repeating CORE_BASE writes */
        {
            ath10k_pci_enable_legacy_irq(ar);
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    } while (time_before(jiffies, timeout));

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
        ath10k_err("failed to receive initialized event from target: %08x\n",
                   val);
        return ZX_ERR_TIMED_OUT;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target initialised\n");
    return ZX_OK;
}

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
    struct pci_dev* pdev = ar_pci->pdev;
    zx_status_t ret;

    pci_set_drvdata(pdev, ar);

    ret = pci_enable_device(pdev);
    if (ret != ZX_OK) {
        ath10k_err("failed to enable pci device: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ret = pci_request_region(pdev, BAR_NUM, "ath");
    if (ret != ZX_OK) {
        ath10k_err("failed to request region BAR%d: %s\n", BAR_NUM,
                   zx_status_get_string(ret));
        goto err_device;
    }

    /* Target expects 32 bit DMA. Enforce it. */
    ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
    if (ret != ZX_OK) {
        ath10k_err("failed to set dma mask to 32-bit: %s\n", zx_status_get_string(ret));
        goto err_region;
    }

    ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
    if (ret != ZX_OK) {
        ath10k_err("failed to set consistent dma mask to 32-bit: %s\n",
                   zx_status_get_string(ret));
        goto err_region;
    }

    pci_set_master(pdev);

    /* Arrange for access to Target SoC registers. */
    ar_pci->mem_len = pci_resource_len(pdev, BAR_NUM);
    ar_pci->mem = pci_iomap(pdev, BAR_NUM, 0);
    if (!ar_pci->mem) {
        ath10k_err("failed to iomap BAR%d\n", BAR_NUM);
        ret = ZX_ERR_IO;
        goto err_master;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot pci_mem 0x%pK\n", ar_pci->mem);
    return ZX_OK;

err_master:
    pci_clear_master(pdev);

err_region:
    pci_release_region(pdev, BAR_NUM);

err_device:
    pci_disable_device(pdev);

    return ret;
}

static void ath10k_pci_release(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    struct pci_dev* pdev = ar_pci->pdev;

    pci_iounmap(pdev, ar_pci->mem);
    pci_release_region(pdev, BAR_NUM);
    pci_clear_master(pdev);
    pci_disable_device(pdev);
}

static bool ath10k_pci_chip_is_supported(uint32_t dev_id, uint32_t chip_id) {
    const struct ath10k_pci_supp_chip* supp_chip;
    int i;
    uint32_t rev_id = MS(chip_id, SOC_CHIP_ID_REV);

    for (i = 0; i < countof(ath10k_pci_supp_chips); i++) {
        supp_chip = &ath10k_pci_supp_chips[i];

        if (supp_chip->dev_id == dev_id &&
                supp_chip->rev_id == rev_id) {
            return true;
        }
    }

    return false;
}

zx_status_t ath10k_pci_setup_resource(struct ath10k* ar) {
    struct ath10k_pci* ar_pci = ath10k_pci_priv(ar);
    zx_status_t ret;

    mtx_init(&ar_pci->ce_lock, mtx_plain);
    mtx_init(&ar_pci->ps_lock, mtx_plain);

    setup_timer(&ar_pci->rx_post_retry, ath10k_pci_rx_replenish_retry,
                (unsigned long)ar);

    if (QCA_REV_6174(ar) || QCA_REV_9377(ar)) {
        ath10k_pci_override_ce_config(ar);
    }

    ret = ath10k_pci_alloc_pipes(ar);
    if (ret != ZX_OK) {
        ath10k_err("failed to allocate copy engine pipes: %s\n",
                   zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

void ath10k_pci_release_resource(struct ath10k* ar) {
    ath10k_pci_rx_retry_sync(ar);
    netif_napi_del(&ar->napi);
    ath10k_pci_ce_deinit(ar);
    ath10k_pci_free_pipes(ar);
}

static const struct ath10k_bus_ops ath10k_pci_bus_ops = {
    .read32         = ath10k_bus_pci_read32,
    .write32        = ath10k_bus_pci_write32,
    .get_num_banks  = ath10k_pci_get_num_banks,
};
#endif

static zx_status_t ath10k_pci_probe(void* ctx, zx_device_t* dev) {
    return ZX_OK;
#if 0 // NEEDS PORTING
    zx_status_t ret = 0;
    struct ath10k* ar;
    struct ath10k_pci* ar_pci;
    enum ath10k_hw_rev hw_rev;
    uint32_t chip_id;
    bool pci_ps;
    int (*pci_soft_reset)(struct ath10k *ar);
    int (*pci_hard_reset)(struct ath10k *ar);
    zx_status_t (*targ_cpu_to_ce_addr)(struct ath10k *ar, uint32_t addr, uint32_t* ce_addr);

    switch (pci_dev->device) {
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
        WARN_ON(1);
        return ZX_ERR_NOT_SUPPORTED;
    }

    ar = ath10k_core_create(sizeof(*ar_pci), &pdev->dev, ATH10K_BUS_PCI,
                            hw_rev, &ath10k_pci_hif_ops);
    if (!ar) {
        dev_err(&pdev->dev, "failed to allocate core\n");
        return ZX_ERR_NO_MEMORY;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "pci probe %04x:%04x %04x:%04x\n",
               pdev->vendor, pdev->device,
               pdev->subsystem_vendor, pdev->subsystem_device);

    ar_pci = ath10k_pci_priv(ar);
    ar_pci->pdev = pdev;
    ar_pci->dev = &pdev->dev;
    ar_pci->ar = ar;
    ar->dev_id = pci_dev->device;
    ar_pci->pci_ps = pci_ps;
    ar_pci->bus_ops = &ath10k_pci_bus_ops;
    ar_pci->pci_soft_reset = pci_soft_reset;
    ar_pci->pci_hard_reset = pci_hard_reset;
    ar_pci->targ_cpu_to_ce_addr = targ_cpu_to_ce_addr;

    ar->id.vendor = pdev->vendor;
    ar->id.device = pdev->device;
    ar->id.subsystem_vendor = pdev->subsystem_vendor;
    ar->id.subsystem_device = pdev->subsystem_device;

    setup_timer(&ar_pci->ps_timer, ath10k_pci_ps_timer,
                (unsigned long)ar);

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
                 ath10k_pci_get_irq_method(ar), ar_pci->oper_irq_mode,
                 ath10k_pci_irq_mode, ath10k_pci_reset_mode);

    ret = ath10k_pci_request_irq(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to request irqs: %s\n", zx_status_get_string(ret));
        goto err_deinit_irq;
    }

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

    if (!ath10k_pci_chip_is_supported(pdev->device, chip_id)) {
        ath10k_err("device %04x with chip_id %08x isn't supported\n",
                   pdev->device, chip_id);
        goto err_free_irq;
    }

    ret = ath10k_core_register(ar, chip_id);
    if (ret != ZX_OK) {
        ath10k_err("failed to register driver core: %s\n", zx_status_get_string(ret));
        goto err_free_irq;
    }

    return ZX_OK;

err_free_irq:
    ath10k_pci_free_irq(ar);
    ath10k_pci_rx_retry_sync(ar);

err_deinit_irq:
    ath10k_pci_deinit_irq(ar);

err_sleep:
    ath10k_pci_sleep_sync(ar);
    ath10k_pci_release(ar);

err_free_pipes:
    ath10k_pci_free_pipes(ar);

err_core_destroy:
    ath10k_core_destroy(ar);

    return ret;
#endif // NEEDS PORTING
}

#if 0 // NEEDS PORTING
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
#endif // NEEDS PORTING

static zx_driver_ops_t ath10k_pci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ath10k_pci_probe,
};

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

