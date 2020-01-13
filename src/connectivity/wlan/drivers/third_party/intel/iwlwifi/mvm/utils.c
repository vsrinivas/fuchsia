/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/rs.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

/*
 * Will return ZX_OK even if the cmd failed when ZX_ERR_BAD_STATE is asserted unless
 * CMD_WANT_SKB is set in cmd->flags.
 */
zx_status_t iwl_mvm_send_cmd(struct iwl_mvm* mvm, struct iwl_host_cmd* cmd) {
  zx_status_t ret;

#if defined(CPTCFG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
  if (WARN_ON(mvm->d3_test_active)) {
    return -EIO;
  }
#endif

  /*
   * Synchronous commands from this op-mode must hold
   * the mutex, this ensures we don't try to send two
   * (or more) synchronous commands at a time.
   */
  if (!(cmd->flags & CMD_ASYNC)) {
    lockdep_assert_held(&mvm->mutex);
    if (!(cmd->flags & CMD_SEND_IN_IDLE)) {
      iwl_mvm_ref(mvm, IWL_MVM_REF_SENDING_CMD);
    }
  }

  ret = iwl_trans_send_cmd(mvm->trans, cmd);

  if (!(cmd->flags & (CMD_ASYNC | CMD_SEND_IN_IDLE))) {
    iwl_mvm_unref(mvm, IWL_MVM_REF_SENDING_CMD);
  }

  /*
   * If the caller wants the SKB, then don't hide any problems, the
   * caller might access the response buffer which will be NULL if
   * the command failed.
   */
  if (cmd->flags & CMD_WANT_SKB) {
    return ret;
  }

  /* Silently ignore failures if ZX_ERR_BAD_STATE is asserted */
  if (ret == ZX_ERR_BAD_STATE) {
    return ZX_OK;
  }

  return ret;
}

zx_status_t iwl_mvm_send_cmd_pdu(struct iwl_mvm* mvm, uint32_t id, uint32_t flags, uint16_t len,
                                 const void* data) {
  struct iwl_host_cmd cmd = {
      .id = id,
      .len =
          {
              len,
          },
      .data =
          {
              data,
          },
      .flags = flags,
  };

  return iwl_mvm_send_cmd(mvm, &cmd);
}

/*
 * We assume that the caller set the status to the success value
 */
zx_status_t iwl_mvm_send_cmd_status(struct iwl_mvm* mvm, struct iwl_host_cmd* cmd,
                                    uint32_t* status) {
  struct iwl_rx_packet* pkt;
  struct iwl_cmd_response* resp;
  int ret, resp_len;

  lockdep_assert_held(&mvm->mutex);

#if defined(CPTCFG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
  if (WARN_ON(mvm->d3_test_active)) {
    return -EIO;
  }
#endif

  /*
   * Only synchronous commands can wait for status, we use WANT_SKB so the caller can't.
   */
  if (cmd->flags & (CMD_ASYNC | CMD_WANT_SKB)) {
    IWL_WARN(mvm, "cmd flags 0x%x\n", cmd->flags);
    return ZX_ERR_INVALID_ARGS;
  }

  cmd->flags |= CMD_WANT_SKB;

  ret = iwl_trans_send_cmd(mvm->trans, cmd);
  if (ret == ZX_ERR_BAD_STATE) {
    /*
     * The command failed because of ZX_ERR_BAD_STATE(RFKILL), don't update
     * the status, leave it as success and return 0.
     */
    return ZX_OK;
  } else if (ret != ZX_OK) {
    return ret;
  }

  pkt = cmd->resp_pkt;

  resp_len = iwl_rx_packet_payload_len(pkt);
  if (resp_len != sizeof(*resp)) {
    IWL_WARN(mvm, "Rx packet payload length is not expected. expected: %lu, actual: %d\n",
             sizeof(*resp), resp_len);
    ret = ZX_ERR_IO;
    goto out_free_resp;
  }

  resp = (void*)pkt->data;
  *status = le32_to_cpu(resp->status);
out_free_resp:
  iwl_free_resp(cmd);
  return ret;
}

/*
 * We assume that the caller set the status to the sucess value
 */
zx_status_t iwl_mvm_send_cmd_pdu_status(struct iwl_mvm* mvm, uint32_t id, uint16_t len,
                                        const void* data, uint32_t* status) {
  struct iwl_host_cmd cmd = {
      .id = id,
      .len =
          {
              len,
          },
      .data =
          {
              data,
          },
  };

  return iwl_mvm_send_cmd_status(mvm, &cmd, status);
}

//
// mac80211_idx is actually an iwlwifi thing. It has different meanings in 2.4 GHz and 5 GHz, which
// is confusing:
//
//   2.4 GHz:  0 ~ 11  (CCK and OFDM: 1 Mbps ~ 54 Mbps)
//   5   GHz:  0 ~ 7   (OFDM only: 6 Mbps ~ 54 Mbps)
//
static const uint32_t mac80211_idx_to_data_rate_[] = {
    //
    //                    mac80211 idx    data rate
    //                   2.4 Ghz | 5 Ghz
    // CCK
    TO_HALF_MBPS(1),   //  0                 1 Mbps
    TO_HALF_MBPS(2),   //  1                 2 Mbps
    TO_HALF_MBPS(5),   //  2                 5 Mbps
    TO_HALF_MBPS(11),  //  3                11 Mbps
    // OFDM
    TO_HALF_MBPS(6),   //  4        0        6 Mbps
    TO_HALF_MBPS(9),   //  5        1        9 Mbps
    TO_HALF_MBPS(12),  //  6        2       12 Mbps
    TO_HALF_MBPS(18),  //  7        3       18 Mbps
    TO_HALF_MBPS(24),  //  8        4       24 Mbps
    TO_HALF_MBPS(36),  //  9        5       36 Mbps
    TO_HALF_MBPS(48),  // 10        6       48 Mbps
    TO_HALF_MBPS(54),  // 11        7       54 Mbps
};

// Converts mac80211_idx to data rate (used by MLME).
//
// Args:
//   band: 2.4 Ghz or 5 GHz band.
//   mac_idx: set iwl_chan_idx above.
//
// Returns:
//   data_rate: in 0.5 Mbps unit. For data_rate of 'struct wlan_rx_info'.
//
zx_status_t mac80211_idx_to_data_rate(wlan_info_band_t band, int mac_idx, uint32_t* data_rate) {
  int band_offset;

  if (band == WLAN_INFO_BAND_2GHZ) {
    band_offset = 0;
  } else if (band == WLAN_INFO_BAND_5GHZ) {
    band_offset = IWL_FIRST_OFDM_RATE;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t idx = band_offset + mac_idx;
  if (idx >= ARRAY_SIZE(mac80211_idx_to_data_rate_)) {
    return ZX_ERR_INVALID_ARGS;
  }

  *data_rate = mac80211_idx_to_data_rate_[idx];

  return ZX_OK;
}

#define IWL_DECLARE_RATE_INFO(r) [IWL_RATE_##r##M_INDEX] = IWL_RATE_##r##M_PLCP

/*
 * Translate from fw_rate_index (IWL_RATE_XXM_INDEX) to PLCP
 */
static const uint8_t fw_rate_idx_to_plcp[IWL_RATE_COUNT] = {
    IWL_DECLARE_RATE_INFO(1),  IWL_DECLARE_RATE_INFO(2),  IWL_DECLARE_RATE_INFO(5),
    IWL_DECLARE_RATE_INFO(11), IWL_DECLARE_RATE_INFO(6),  IWL_DECLARE_RATE_INFO(9),
    IWL_DECLARE_RATE_INFO(12), IWL_DECLARE_RATE_INFO(18), IWL_DECLARE_RATE_INFO(24),
    IWL_DECLARE_RATE_INFO(36), IWL_DECLARE_RATE_INFO(48), IWL_DECLARE_RATE_INFO(54),
};

// Converts MVM's legacy rate to iwl chan index.
//
// Args:
//   rate_n_flags: see RATE_LEGACY_RATE_MSK in fw/api/rs.h
//   band: 2.4 Ghz or 5 GHz band.
//
// Returns:
//   ptr_chan_idx: iwl chan index where valid values are:
//
//     0~11 in 2.4 GHz band
//     0~7  in   5 GHz band
//
zx_status_t iwl_mvm_legacy_rate_to_mac80211_idx(uint32_t rate_n_flags, wlan_info_band_t band,
                                                int* ptr_chan_idx) {
  int rate = rate_n_flags & RATE_LEGACY_RATE_MSK;
  int band_offset = 0;

  // Sanity-check
  if (band >= WLAN_INFO_BAND_COUNT) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!ptr_chan_idx) {
    return ZX_ERR_INVALID_ARGS;
  }
#if 0   // NEEDS_PORTING
  if (band == NL80211_BAND_60GHZ) {
    return ZX_ERR_NOT_SUPPORTED;
  }
#endif  // NEEDS_PORTING

  /* Legacy rate format, search for match in table */
  if (band == WLAN_INFO_BAND_5GHZ) {
    band_offset = IWL_FIRST_OFDM_RATE;
  }
  for (int chan_idx = band_offset; chan_idx < IWL_RATE_COUNT_LEGACY; chan_idx++) {
    if (fw_rate_idx_to_plcp[chan_idx] == rate) {
      *ptr_chan_idx = chan_idx - band_offset;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

uint8_t iwl_mvm_mac80211_idx_to_hwrate(int rate_idx) {
  /* Get PLCP rate for tx_cmd->rate_n_flags */
  return fw_rate_idx_to_plcp[rate_idx];
}

void iwl_mvm_rx_fw_error(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_error_resp* err_resp = (void*)pkt->data;

  IWL_ERR(mvm, "FW Error notification: type 0x%08X cmd_id 0x%02X\n",
          le32_to_cpu(err_resp->error_type), err_resp->cmd_id);
  IWL_ERR(mvm, "FW Error notification: seq 0x%04X service 0x%08X\n",
          le16_to_cpu(err_resp->bad_cmd_seq_num), le32_to_cpu(err_resp->error_service));
  IWL_ERR(mvm, "FW Error notification: timestamp 0x%016lX\n", le64_to_cpu(err_resp->timestamp));
}

/*
 * Returns the first antenna as ANT_[ABC], as defined in iwl-config.h.
 * The parameter should also be a combination of ANT_[ABC].
 */
uint8_t first_antenna(uint8_t mask) {
  BUILD_BUG_ON(ANT_A != BIT(0)); /* using ffs is wrong if not */
  if (WARN_ON_ONCE(!mask)) {     /* ffs will return 0 if mask is zeroed */
    return BIT(0);
  }
  return BIT(ffs(mask) - 1);
}

/*
 * Toggles between TX antennas to send the probe request on.
 * Receives the bitmask of valid TX antennas and the *index* used
 * for the last TX, and returns the next valid *index* to use.
 * In order to set it in the tx_cmd, must do BIT(idx).
 */
uint8_t iwl_mvm_next_antenna(struct iwl_mvm* mvm, uint8_t valid, uint8_t last_idx) {
  uint8_t ind = last_idx;
  int i;

  for (i = 0; i < MAX_ANT_NUM; i++) {
    ind = (ind + 1) % MAX_ANT_NUM;
    if (valid & BIT(ind)) {
      return ind;
    }
  }

  IWL_WARN(mvm, "Failed to toggle between antennas 0x%x\n", valid);
  return last_idx;
}

#define FW_SYSASSERT_CPU_MASK 0xf0000000
static const struct {
  const char* name;
  uint8_t num;
} advanced_lookup[] = {
    {"NMI_INTERRUPT_WDG", 0x34},
    {"SYSASSERT", 0x35},
    {"UCODE_VERSION_MISMATCH", 0x37},
    {"BAD_COMMAND", 0x38},
    {"NMI_INTERRUPT_DATA_ACTION_PT", 0x3C},
    {"FATAL_ERROR", 0x3D},
    {"NMI_TRM_HW_ERR", 0x46},
    {"NMI_INTERRUPT_TRM", 0x4C},
    {"NMI_INTERRUPT_BREAK_POINT", 0x54},
    {"NMI_INTERRUPT_WDG_RXF_FULL", 0x5C},
    {"NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64},
    {"NMI_INTERRUPT_HOST", 0x66},
    {"NMI_INTERRUPT_LMAC_FATAL", 0x70},
    {"NMI_INTERRUPT_UMAC_FATAL", 0x71},
    {"NMI_INTERRUPT_OTHER_LMAC_FATAL", 0x73},
    {"NMI_INTERRUPT_ACTION_PT", 0x7C},
    {"NMI_INTERRUPT_UNKNOWN", 0x84},
    {"NMI_INTERRUPT_INST_ACTION_PT", 0x86},
    {"ADVANCED_SYSASSERT", 0},
};

static const char* desc_lookup(uint32_t num) {
  size_t i;

  for (i = 0; i < ARRAY_SIZE(advanced_lookup) - 1; i++)
    if (advanced_lookup[i].num == (num & ~FW_SYSASSERT_CPU_MASK)) {
      return advanced_lookup[i].name;
    }

  /* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
  return advanced_lookup[i].name;
}

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with uint32_t-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_error_event_table_v1 {
  uint32_t valid;          /* (nonzero) valid, (0) log is empty */
  uint32_t error_id;       /* type of error */
  uint32_t pc;             /* program counter */
  uint32_t blink1;         /* branch link */
  uint32_t blink2;         /* branch link */
  uint32_t ilink1;         /* interrupt link */
  uint32_t ilink2;         /* interrupt link */
  uint32_t data1;          /* error-specific data */
  uint32_t data2;          /* error-specific data */
  uint32_t data3;          /* error-specific data */
  uint32_t bcon_time;      /* beacon timer */
  uint32_t tsf_low;        /* network timestamp function timer */
  uint32_t tsf_hi;         /* network timestamp function timer */
  uint32_t gp1;            /* GP1 timer register */
  uint32_t gp2;            /* GP2 timer register */
  uint32_t gp3;            /* GP3 timer register */
  uint32_t ucode_ver;      /* uCode version */
  uint32_t hw_ver;         /* HW Silicon version */
  uint32_t brd_ver;        /* HW board version */
  uint32_t log_pc;         /* log program counter */
  uint32_t frame_ptr;      /* frame pointer */
  uint32_t stack_ptr;      /* stack pointer */
  uint32_t hcmd;           /* last host command header */
  uint32_t isr0;           /* isr status register LMPM_NIC_ISR0:
                            * rxtx_flag */
  uint32_t isr1;           /* isr status register LMPM_NIC_ISR1:
                            * host_flag */
  uint32_t isr2;           /* isr status register LMPM_NIC_ISR2:
                            * enc_flag */
  uint32_t isr3;           /* isr status register LMPM_NIC_ISR3:
                            * time_flag */
  uint32_t isr4;           /* isr status register LMPM_NIC_ISR4:
                            * wico interrupt */
  uint32_t isr_pref;       /* isr status register LMPM_NIC_PREF_STAT */
  uint32_t wait_event;     /* wait event() caller address */
  uint32_t l2p_control;    /* L2pControlField */
  uint32_t l2p_duration;   /* L2pDurationField */
  uint32_t l2p_mhvalid;    /* L2pMhValidBits */
  uint32_t l2p_addr_match; /* L2pAddrMatchStat */
  uint32_t lmpm_pmg_sel;   /* indicate which clocks are turned on
                            * (LMPM_PMG_SEL) */
  uint32_t u_timestamp;    /* indicate when the date and time of the
                            * compilation */
  uint32_t flow_handler;   /* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_1 */;

struct iwl_error_event_table {
  uint32_t valid;          /* (nonzero) valid, (0) log is empty */
  uint32_t error_id;       /* type of error */
  uint32_t trm_hw_status0; /* TRM HW status */
  uint32_t trm_hw_status1; /* TRM HW status */
  uint32_t blink2;         /* branch link */
  uint32_t ilink1;         /* interrupt link */
  uint32_t ilink2;         /* interrupt link */
  uint32_t data1;          /* error-specific data */
  uint32_t data2;          /* error-specific data */
  uint32_t data3;          /* error-specific data */
  uint32_t bcon_time;      /* beacon timer */
  uint32_t tsf_low;        /* network timestamp function timer */
  uint32_t tsf_hi;         /* network timestamp function timer */
  uint32_t gp1;            /* GP1 timer register */
  uint32_t gp2;            /* GP2 timer register */
  uint32_t fw_rev_type;    /* firmware revision type */
  uint32_t major;          /* uCode version major */
  uint32_t minor;          /* uCode version minor */
  uint32_t hw_ver;         /* HW Silicon version */
  uint32_t brd_ver;        /* HW board version */
  uint32_t log_pc;         /* log program counter */
  uint32_t frame_ptr;      /* frame pointer */
  uint32_t stack_ptr;      /* stack pointer */
  uint32_t hcmd;           /* last host command header */
  uint32_t isr0;           /* isr status register LMPM_NIC_ISR0:
                            * rxtx_flag */
  uint32_t isr1;           /* isr status register LMPM_NIC_ISR1:
                            * host_flag */
  uint32_t isr2;           /* isr status register LMPM_NIC_ISR2:
                            * enc_flag */
  uint32_t isr3;           /* isr status register LMPM_NIC_ISR3:
                            * time_flag */
  uint32_t isr4;           /* isr status register LMPM_NIC_ISR4:
                            * wico interrupt */
  uint32_t last_cmd_id;    /* last HCMD id handled by the firmware */
  uint32_t wait_event;     /* wait event() caller address */
  uint32_t l2p_control;    /* L2pControlField */
  uint32_t l2p_duration;   /* L2pDurationField */
  uint32_t l2p_mhvalid;    /* L2pMhValidBits */
  uint32_t l2p_addr_match; /* L2pAddrMatchStat */
  uint32_t lmpm_pmg_sel;   /* indicate which clocks are turned on
                            * (LMPM_PMG_SEL) */
  uint32_t u_timestamp;    /* indicate when the date and time of the
                            * compilation */
  uint32_t flow_handler;   /* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_3 */;

/*
 * UMAC error struct - relevant starting from family 8000 chip.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with uint32_t-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_umac_error_event_table {
  uint32_t valid;    /* (nonzero) valid, (0) log is empty */
  uint32_t error_id; /* type of error */
  uint32_t blink1;   /* branch link */
  uint32_t blink2;   /* branch link */
  uint32_t ilink1;   /* interrupt link */
  uint32_t ilink2;   /* interrupt link */
  uint32_t data1;    /* error-specific data */
  uint32_t data2;    /* error-specific data */
  uint32_t data3;    /* error-specific data */
  uint32_t umac_major;
  uint32_t umac_minor;
  uint32_t frame_pointer; /* core register 27*/
  uint32_t stack_pointer; /* core register 28 */
  uint32_t cmd_header;    /* latest host cmd sent to UMAC */
  uint32_t nic_isr_pref;  /* ISR status register */
} __packed;

#define ERROR_START_OFFSET (1 * sizeof(uint32_t))
#define ERROR_ELEM_SIZE (7 * sizeof(uint32_t))

static void iwl_mvm_dump_umac_error_log(struct iwl_mvm* mvm) {
  struct iwl_trans* trans = mvm->trans;
  struct iwl_umac_error_event_table table;

  if (!mvm->support_umac_log) {
    return;
  }

  iwl_trans_read_mem_bytes(trans, mvm->umac_error_event_table, &table, sizeof(table));

  if (table.valid) {
    mvm->fwrt.dump.umac_err_id = table.error_id;
  }

  if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
    IWL_ERR(trans, "Start IWL Error Log Dump:\n");
    IWL_ERR(trans, "Status: 0x%08lX, count: %d\n", mvm->status, table.valid);
  }

  IWL_ERR(mvm, "0x%08X | %s\n", table.error_id, desc_lookup(table.error_id));
  IWL_ERR(mvm, "0x%08X | umac branchlink1\n", table.blink1);
  IWL_ERR(mvm, "0x%08X | umac branchlink2\n", table.blink2);
  IWL_ERR(mvm, "0x%08X | umac interruptlink1\n", table.ilink1);
  IWL_ERR(mvm, "0x%08X | umac interruptlink2\n", table.ilink2);
  IWL_ERR(mvm, "0x%08X | umac data1\n", table.data1);
  IWL_ERR(mvm, "0x%08X | umac data2\n", table.data2);
  IWL_ERR(mvm, "0x%08X | umac data3\n", table.data3);
  IWL_ERR(mvm, "0x%08X | umac major\n", table.umac_major);
  IWL_ERR(mvm, "0x%08X | umac minor\n", table.umac_minor);
  IWL_ERR(mvm, "0x%08X | frame pointer\n", table.frame_pointer);
  IWL_ERR(mvm, "0x%08X | stack pointer\n", table.stack_pointer);
  IWL_ERR(mvm, "0x%08X | last host cmd\n", table.cmd_header);
  IWL_ERR(mvm, "0x%08X | isr status reg\n", table.nic_isr_pref);
}

static void iwl_mvm_dump_lmac_error_log(struct iwl_mvm* mvm, uint8_t lmac_num) {
  struct iwl_trans* trans = mvm->trans;
  struct iwl_error_event_table table;
  uint32_t val, base = mvm->error_event_table[lmac_num];

  if (mvm->fwrt.cur_fw_img == IWL_UCODE_INIT) {
    if (!base) {
      base = mvm->fw->init_errlog_ptr;
    }
  } else {
    if (!base) {
      base = mvm->fw->inst_errlog_ptr;
    }
  }

  if (base < 0x400000) {
    IWL_ERR(mvm, "Not valid error log pointer 0x%08X for %s uCode\n", base,
            (mvm->fwrt.cur_fw_img == IWL_UCODE_INIT) ? "Init" : "RT");
    return;
  }

  /* check if there is a HW error */
  val = iwl_trans_read_mem32(trans, base);
  if (((val & ~0xf) == 0xa5a5a5a0) || ((val & ~0xf) == 0x5a5a5a50)) {
    int err;

    IWL_ERR(trans, "HW error, resetting before reading\n");

    /* reset the device */
    iwl_trans_sw_reset(trans);

    /* set INIT_DONE flag */
    iwl_set_bit(trans, CSR_GP_CNTRL, BIT(trans->cfg->csr->flag_init_done));

    /* and wait for clock stabilization */
    if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000) {
      zx_nanosleep(zx_deadline_after(ZX_USEC(2)));
    }

    err = iwl_poll_bit(trans, CSR_GP_CNTRL, BIT(trans->cfg->csr->flag_mac_clock_ready),
                       BIT(trans->cfg->csr->flag_mac_clock_ready), 25000 /* usecs */, NULL);
    if (err != ZX_OK) {
      IWL_DEBUG_INFO(trans, "Failed to reset the card for the dump\n");
      return;
    }
  }

  iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

  if (table.valid) {
    mvm->fwrt.dump.lmac_err_id[lmac_num] = table.error_id;
  }

  if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
    IWL_ERR(trans, "Start IWL Error Log Dump:\n");
    IWL_ERR(trans, "Status: 0x%08lX, count: %d\n", mvm->status, table.valid);
  }

  /* Do not change this output - scripts rely on it */

  IWL_ERR(mvm, "Loaded firmware version: %s\n", mvm->fw->fw_version);

  IWL_ERR(mvm, "0x%08X | %-28s\n", table.error_id, desc_lookup(table.error_id));
  IWL_ERR(mvm, "0x%08X | trm_hw_status0\n", table.trm_hw_status0);
  IWL_ERR(mvm, "0x%08X | trm_hw_status1\n", table.trm_hw_status1);
  IWL_ERR(mvm, "0x%08X | branchlink2\n", table.blink2);
  IWL_ERR(mvm, "0x%08X | interruptlink1\n", table.ilink1);
  IWL_ERR(mvm, "0x%08X | interruptlink2\n", table.ilink2);
  IWL_ERR(mvm, "0x%08X | data1\n", table.data1);
  IWL_ERR(mvm, "0x%08X | data2\n", table.data2);
  IWL_ERR(mvm, "0x%08X | data3\n", table.data3);
  IWL_ERR(mvm, "0x%08X | beacon time\n", table.bcon_time);
  IWL_ERR(mvm, "0x%08X | tsf low\n", table.tsf_low);
  IWL_ERR(mvm, "0x%08X | tsf hi\n", table.tsf_hi);
  IWL_ERR(mvm, "0x%08X | time gp1\n", table.gp1);
  IWL_ERR(mvm, "0x%08X | time gp2\n", table.gp2);
  IWL_ERR(mvm, "0x%08X | uCode revision type\n", table.fw_rev_type);
  IWL_ERR(mvm, "0x%08X | uCode version major\n", table.major);
  IWL_ERR(mvm, "0x%08X | uCode version minor\n", table.minor);
  IWL_ERR(mvm, "0x%08X | hw version\n", table.hw_ver);
  IWL_ERR(mvm, "0x%08X | board version\n", table.brd_ver);
  IWL_ERR(mvm, "0x%08X | hcmd\n", table.hcmd);
  IWL_ERR(mvm, "0x%08X | isr0\n", table.isr0);
  IWL_ERR(mvm, "0x%08X | isr1\n", table.isr1);
  IWL_ERR(mvm, "0x%08X | isr2\n", table.isr2);
  IWL_ERR(mvm, "0x%08X | isr3\n", table.isr3);
  IWL_ERR(mvm, "0x%08X | isr4\n", table.isr4);
  IWL_ERR(mvm, "0x%08X | last cmd Id\n", table.last_cmd_id);
  IWL_ERR(mvm, "0x%08X | wait_event\n", table.wait_event);
  IWL_ERR(mvm, "0x%08X | l2p_control\n", table.l2p_control);
  IWL_ERR(mvm, "0x%08X | l2p_duration\n", table.l2p_duration);
  IWL_ERR(mvm, "0x%08X | l2p_mhvalid\n", table.l2p_mhvalid);
  IWL_ERR(mvm, "0x%08X | l2p_addr_match\n", table.l2p_addr_match);
  IWL_ERR(mvm, "0x%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
  IWL_ERR(mvm, "0x%08X | timestamp\n", table.u_timestamp);
  IWL_ERR(mvm, "0x%08X | flow_handler\n", table.flow_handler);
}

void iwl_mvm_dump_nic_error_log(struct iwl_mvm* mvm) {
  if (!test_bit(STATUS_DEVICE_ENABLED, &mvm->trans->status)) {
    IWL_ERR(mvm, "DEVICE_ENABLED bit is not set. Aborting dump.\n");
    return;
  }

  iwl_mvm_dump_lmac_error_log(mvm, 0);

  if (mvm->error_event_table[1]) {
    iwl_mvm_dump_lmac_error_log(mvm, 1);
  }

  iwl_mvm_dump_umac_error_log(mvm);
}

zx_status_t iwl_mvm_reconfig_scd(struct iwl_mvm* mvm, int queue, int fifo, int sta_id, int tid,
                                 int frame_limit, uint16_t ssn) {
  struct iwl_scd_txq_cfg_cmd cmd = {
      .scd_queue = queue,
      .action = SCD_CFG_ENABLE_QUEUE,
      .window = frame_limit,
      .sta_id = sta_id,
      .ssn = cpu_to_le16(ssn),
      .tx_fifo = fifo,
      .aggregate = (queue >= IWL_MVM_DQA_MIN_DATA_QUEUE || queue == IWL_MVM_DQA_BSS_CLIENT_QUEUE),
      .tid = tid,
  };
  zx_status_t ret;

  if (iwl_mvm_has_new_tx_api(mvm) != ZX_OK) {
    IWL_WARN(mvm, "iwl_mvm_has_new_tx_api() returns true%s\n", "");
    return ZX_ERR_INVALID_ARGS;
  }

  if (mvm->queue_info[queue].tid_bitmap == 0) {
    IWL_WARN(mvm, "Trying to reconfig unallocated queue %d\n", queue);
    return ZX_ERR_BAD_HANDLE;
  }

  IWL_DEBUG_TX_QUEUES(mvm, "Reconfig SCD for TXQ #%d\n", queue);

  ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd);
  if (ret != ZX_OK) {
    IWL_WARN(mvm, "Failed to re-configure queue %d on FIFO %d, ret=%d\n", queue, fifo, ret);
  }

  return ret;
}

/**
 * iwl_mvm_send_lq_cmd() - Send link quality command
 * @sync: This command can be sent synchronously.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
zx_status_t iwl_mvm_send_lq_cmd(struct iwl_mvm* mvm, struct iwl_lq_cmd* lq, bool sync) {
  return ZX_ERR_NOT_SUPPORTED;
#if 0   // NEEDS_PORTING
    struct iwl_host_cmd cmd = {
        .id = LQ_CMD,
        .len =
            {
                sizeof(struct iwl_lq_cmd),
            },
        .flags = sync ? 0 : CMD_ASYNC,
        .data =
            {
                lq,
            },
    };

    if (WARN_ON(lq->sta_id == IWL_MVM_INVALID_STA || iwl_mvm_has_tlc_offload(mvm))) {
        return -EINVAL;
    }

    return iwl_mvm_send_cmd(mvm, &cmd);
#endif  // NEEDS_PORTING
}

/**
 * iwl_mvm_update_smps - Get a request to change the SMPS mode
 * @req_type: The part of the driver who call for a change.
 * @smps_requests: The request to change the SMPS mode.
 *
 * Get a requst to change the SMPS mode,
 * and change it according to all other requests in the driver.
 */
void iwl_mvm_update_smps(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                         enum iwl_mvm_smps_type_request req_type,
                         enum ieee80211_smps_mode smps_request) {
#if 0   // NEEDS_PORTING
    struct iwl_mvm_vif* mvmvif;
    enum ieee80211_smps_mode smps_mode;
    int i;

    lockdep_assert_held(&mvm->mutex);

    /* SMPS is irrelevant for NICs that don't have at least 2 RX antenna */
    if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1) { return; }

    if (vif->type == NL80211_IFTYPE_AP) {
        smps_mode = IEEE80211_SMPS_OFF;
    } else {
        smps_mode = IEEE80211_SMPS_AUTOMATIC;
    }

    mvmvif = iwl_mvm_vif_from_mac80211(vif);
    mvmvif->smps_requests[req_type] = smps_request;
    for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
        if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC) {
            smps_mode = IEEE80211_SMPS_STATIC;
            break;
        }
        if (mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC) {
            smps_mode = IEEE80211_SMPS_DYNAMIC;
        }
    }

    ieee80211_request_smps(vif, smps_mode);
#endif  // NEEDS_PORTING
}

zx_status_t iwl_mvm_request_statistics(struct iwl_mvm* mvm, bool clear) {
  return ZX_ERR_NOT_SUPPORTED;
#if 0   // NEEDS_PORTING
    struct iwl_statistics_cmd scmd = {
        .flags = clear ? cpu_to_le32(IWL_STATISTICS_FLG_CLEAR) : 0,
    };
    struct iwl_host_cmd cmd = {
        .id = STATISTICS_CMD,
        .len[0] = sizeof(scmd),
        .data[0] = &scmd,
        .flags = CMD_WANT_SKB,
    };
    int ret;

    ret = iwl_mvm_send_cmd(mvm, &cmd);
    if (ret) { return ret; }

    iwl_mvm_handle_rx_statistics(mvm, cmd.resp_pkt);
    iwl_free_resp(&cmd);

    if (clear) { iwl_mvm_accu_radio_stats(mvm); }

    return 0;
#endif  // NEEDS_PORTING
}

void iwl_mvm_accu_radio_stats(struct iwl_mvm* mvm) {
  mvm->accu_radio_stats.rx_time += mvm->radio_stats.rx_time;
  mvm->accu_radio_stats.tx_time += mvm->radio_stats.tx_time;
  mvm->accu_radio_stats.on_time_rf += mvm->radio_stats.on_time_rf;
  mvm->accu_radio_stats.on_time_scan += mvm->radio_stats.on_time_scan;
}

#if 0  // NEEDS_PORTING
static void iwl_mvm_diversity_iter(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    bool* result = _data;
    int i;

    for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
        if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC ||
            mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC) {
            *result = false;
        }
    }
}

bool iwl_mvm_rx_diversity_allowed(struct iwl_mvm* mvm) {
    bool result = true;

    lockdep_assert_held(&mvm->mutex);

    if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1) { return false; }

    if (mvm->cfg->rx_with_siso_diversity) { return false; }

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_diversity_iter, &result);

    return result;
}

void iwl_mvm_send_low_latency_cmd(struct iwl_mvm* mvm, bool low_latency, uint16_t mac_id) {
    struct iwl_mac_low_latency_cmd cmd = {.mac_id = cpu_to_le32(mac_id)};

    if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_DYNAMIC_QUOTA)) { return; }

    if (low_latency) {
        /* currently we don't care about the direction */
        cmd.low_latency_rx = 1;
        cmd.low_latency_tx = 1;
    }

    if (iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(LOW_LATENCY_CMD, MAC_CONF_GROUP, 0), 0, sizeof(cmd),
                             &cmd)) {
        IWL_ERR(mvm, "Failed to send low latency command\n");
    }
}

int iwl_mvm_update_low_latency(struct iwl_mvm* mvm, struct ieee80211_vif* vif, bool low_latency,
                               enum iwl_mvm_low_latency_cause cause) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    int res;
    bool prev;

    lockdep_assert_held(&mvm->mutex);

    prev = iwl_mvm_vif_low_latency(mvmvif);
    iwl_mvm_vif_set_low_latency(mvmvif, low_latency, cause);

    low_latency = iwl_mvm_vif_low_latency(mvmvif);

    if (low_latency == prev) { return 0; }

    iwl_mvm_send_low_latency_cmd(mvm, low_latency, mvmvif->id);

    res = iwl_mvm_update_quotas(mvm, false, NULL);
    if (res) { return res; }

    iwl_mvm_bt_coex_vif_change(mvm);

#ifdef CPTCFG_IWLMVM_VENDOR_CMDS
    iwl_mvm_send_tcm_event(mvm, vif);
#endif

    return iwl_mvm_power_update_mac(mvm);
}

struct iwl_mvm_low_latency_iter {
    bool result;
    bool result_per_band[NUM_NL80211_BANDS];
};

static void iwl_mvm_ll_iter(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_mvm_low_latency_iter* result = _data;
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    enum nl80211_band band;

    if (iwl_mvm_vif_low_latency(mvmvif)) {
        result->result = true;

        if (!mvmvif->phy_ctxt) { return; }

        band = mvmvif->phy_ctxt->channel->band;
        result->result_per_band[band] = true;
    }
}

bool iwl_mvm_low_latency(struct iwl_mvm* mvm) {
    struct iwl_mvm_low_latency_iter data = {};

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_ll_iter, &data);

    return data.result;
}

bool iwl_mvm_low_latency_band(struct iwl_mvm* mvm, enum nl80211_band band) {
    struct iwl_mvm_low_latency_iter data = {};

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_ll_iter, &data);

    return data.result_per_band[band];
}

struct iwl_bss_iter_data {
    struct ieee80211_vif* vif;
    bool error;
};

static void iwl_mvm_bss_iface_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_bss_iter_data* data = _data;

    if (vif->type != NL80211_IFTYPE_STATION || vif->p2p) { return; }

    if (data->vif) {
        data->error = true;
        return;
    }

    data->vif = vif;
}

struct ieee80211_vif* iwl_mvm_get_bss_vif(struct iwl_mvm* mvm) {
    struct iwl_bss_iter_data bss_iter_data = {};

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_bss_iface_iterator, &bss_iter_data);

    if (bss_iter_data.error) {
        IWL_ERR(mvm, "More than one managed interface active!\n");
        return ERR_PTR(-EINVAL);
    }

    return bss_iter_data.vif;
}

struct iwl_sta_iter_data {
    bool assoc;
};

static void iwl_mvm_sta_iface_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_sta_iter_data* data = _data;

    if (vif->type != NL80211_IFTYPE_STATION) { return; }

    if (vif->bss_conf.assoc) { data->assoc = true; }
}

bool iwl_mvm_is_vif_assoc(struct iwl_mvm* mvm) {
    struct iwl_sta_iter_data data = {
        .assoc = false,
    };

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_sta_iface_iterator, &data);
    return data.assoc;
}

unsigned int iwl_mvm_get_wd_timeout(struct iwl_mvm* mvm, struct ieee80211_vif* vif, bool tdls,
                                    bool cmd_q) {
    struct iwl_fw_dbg_trigger_tlv* trigger;
    struct iwl_fw_dbg_trigger_txq_timer* txq_timer;
    unsigned int default_timeout = cmd_q ? IWL_DEF_WD_TIMEOUT : mvm->cfg->base_params->wd_timeout;

    if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS)) {
        /*
         * We can't know when the station is asleep or awake, so we
         * must disable the queue hang detection.
         */
        if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_STA_PM_NOTIF) && vif &&
            vif->type == NL80211_IFTYPE_AP) {
            return IWL_WATCHDOG_DISABLED;
        }
        return iwlmvm_mod_params.tfd_q_hang_detect ? default_timeout : IWL_WATCHDOG_DISABLED;
    }

    trigger = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS);
    txq_timer = (void*)trigger->data;

    if (tdls) { return le32_to_cpu(txq_timer->tdls); }

    if (cmd_q) { return le32_to_cpu(txq_timer->command_queue); }

    if (WARN_ON(!vif)) { return default_timeout; }

    switch (ieee80211_vif_type_p2p(vif)) {
    case NL80211_IFTYPE_ADHOC:
        return le32_to_cpu(txq_timer->ibss);
    case NL80211_IFTYPE_STATION:
        return le32_to_cpu(txq_timer->bss);
    case NL80211_IFTYPE_AP:
        return le32_to_cpu(txq_timer->softap);
    case NL80211_IFTYPE_P2P_CLIENT:
        return le32_to_cpu(txq_timer->p2p_client);
    case NL80211_IFTYPE_P2P_GO:
        return le32_to_cpu(txq_timer->p2p_go);
    case NL80211_IFTYPE_P2P_DEVICE:
        return le32_to_cpu(txq_timer->p2p_device);
    case NL80211_IFTYPE_MONITOR:
        return default_timeout;
    default:
        WARN_ON(1);
        return mvm->cfg->base_params->wd_timeout;
    }
}

void iwl_mvm_connection_loss(struct iwl_mvm* mvm, struct ieee80211_vif* vif, const char* errmsg) {
    struct iwl_fw_dbg_trigger_tlv* trig;
    struct iwl_fw_dbg_trigger_mlme* trig_mlme;

    trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif), FW_DBG_TRIGGER_MLME);
    if (!trig) { goto out; }

    trig_mlme = (void*)trig->data;

    if (trig_mlme->stop_connection_loss && --trig_mlme->stop_connection_loss) { goto out; }

    iwl_fw_dbg_collect_trig(&mvm->fwrt, trig, "%s", errmsg);

out:
    ieee80211_connection_loss(vif);
}

void iwl_mvm_event_frame_timeout_callback(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                          const struct ieee80211_sta* sta, uint16_t tid) {
    struct iwl_fw_dbg_trigger_tlv* trig;
    struct iwl_fw_dbg_trigger_ba* ba_trig;

    trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif), FW_DBG_TRIGGER_BA);
    if (!trig) { return; }

    ba_trig = (void*)trig->data;

    if (!(le16_to_cpu(ba_trig->frame_timeout) & BIT(tid))) { return; }

    iwl_fw_dbg_collect_trig(&mvm->fwrt, trig, "Frame from %pM timed out, tid %d", sta->addr, tid);
}

uint8_t iwl_mvm_tcm_load_percentage(uint32_t airtime, uint32_t elapsed) {
    if (!elapsed) { return 0; }

    return (100 * airtime / elapsed) / USEC_PER_MSEC;
}

static enum iwl_mvm_traffic_load iwl_mvm_tcm_load(struct iwl_mvm* mvm, uint32_t airtime,
                                                  unsigned long elapsed) {
    uint8_t load = iwl_mvm_tcm_load_percentage(airtime, elapsed);

    if (load > IWL_MVM_TCM_LOAD_HIGH_THRESH) { return IWL_MVM_TRAFFIC_HIGH; }
    if (load > IWL_MVM_TCM_LOAD_MEDIUM_THRESH) { return IWL_MVM_TRAFFIC_MEDIUM; }

    return IWL_MVM_TRAFFIC_LOW;
}

struct iwl_mvm_tcm_iter_data {
    struct iwl_mvm* mvm;
    bool any_sent;
};

static void iwl_mvm_tcm_iter(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_mvm_tcm_iter_data* data = _data;
    struct iwl_mvm* mvm = data->mvm;
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    bool low_latency, prev = mvmvif->low_latency & LOW_LATENCY_TRAFFIC;

    if (mvmvif->id >= NUM_MAC_INDEX_DRIVER) { return; }

    low_latency = mvm->tcm.result.low_latency[mvmvif->id];

    if (!mvm->tcm.result.change[mvmvif->id] && prev == low_latency) {
        iwl_mvm_update_quotas(mvm, false, NULL);
        return;
    }

    if (prev != low_latency) {
        /* this sends traffic load and updates quota as well */
        iwl_mvm_update_low_latency(mvm, vif, low_latency, LOW_LATENCY_TRAFFIC);
    } else {
#ifdef CPTCFG_IWLMVM_VENDOR_CMDS
        iwl_mvm_send_tcm_event(mvm, vif);
#endif
        iwl_mvm_update_quotas(mvm, false, NULL);
    }

    data->any_sent = true;
}

static void iwl_mvm_tcm_results(struct iwl_mvm* mvm) {
    struct iwl_mvm_tcm_iter_data data = {
        .mvm = mvm,
        .any_sent = false,
    };

    mutex_lock(&mvm->mutex);

    ieee80211_iterate_active_interfaces(mvm->hw, IEEE80211_IFACE_ITER_NORMAL, iwl_mvm_tcm_iter,
                                        &data);

#ifdef CPTCFG_IWLMVM_VENDOR_CMDS
    /* send global only */
    if (mvm->tcm.result.global_change && !data.any_sent) { iwl_mvm_send_tcm_event(mvm, NULL); }
#endif

    if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
        iwl_mvm_config_scan(mvm);
    }

    mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_tcm_uapsd_nonagg_detected_wk(struct work_struct* wk) {
    struct iwl_mvm* mvm;
    struct iwl_mvm_vif* mvmvif;
    struct ieee80211_vif* vif;

    mvmvif = container_of(wk, struct iwl_mvm_vif, uapsd_nonagg_detected_wk.work);
    vif = container_of((void*)mvmvif, struct ieee80211_vif, drv_priv);
    mvm = mvmvif->mvm;

    if (mvm->tcm.data[mvmvif->id].opened_rx_ba_sessions) { return; }

    /* remember that this AP is broken */
    memcpy(mvm->uapsd_noagg_bssids[mvm->uapsd_noagg_bssid_write_idx].addr, vif->bss_conf.bssid,
           ETH_ALEN);
    mvm->uapsd_noagg_bssid_write_idx++;
    if (mvm->uapsd_noagg_bssid_write_idx >= IWL_MVM_UAPSD_NOAGG_LIST_LEN) {
        mvm->uapsd_noagg_bssid_write_idx = 0;
    }

    iwl_mvm_connection_loss(mvm, vif, "AP isn't using AMPDU with uAPSD enabled");
}

static void iwl_mvm_uapsd_agg_disconnect_iter(void* data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    struct iwl_mvm* mvm = mvmvif->mvm;
    int* mac_id = data;

    if (vif->type != NL80211_IFTYPE_STATION) { return; }

    if (mvmvif->id != *mac_id) { return; }

    if (!vif->bss_conf.assoc) { return; }

    if (!mvmvif->queue_params[IEEE80211_AC_VO].uapsd &&
        !mvmvif->queue_params[IEEE80211_AC_VI].uapsd &&
        !mvmvif->queue_params[IEEE80211_AC_BE].uapsd &&
        !mvmvif->queue_params[IEEE80211_AC_BK].uapsd) {
        return;
    }

    if (mvm->tcm.data[*mac_id].uapsd_nonagg_detect.detected) { return; }

    mvm->tcm.data[*mac_id].uapsd_nonagg_detect.detected = true;
    IWL_INFO(mvm, "detected AP should do aggregation but isn't, likely due to U-APSD\n");
    schedule_delayed_work(&mvmvif->uapsd_nonagg_detected_wk, 15 * HZ);
}

static void iwl_mvm_check_uapsd_agg_expected_tpt(struct iwl_mvm* mvm, unsigned int elapsed,
                                                 int mac) {
    uint64_t bytes = mvm->tcm.data[mac].uapsd_nonagg_detect.rx_bytes;
    uint64_t tpt;
    unsigned long rate;

    rate = ewma_rate_read(&mvm->tcm.data[mac].uapsd_nonagg_detect.rate);

    if (!rate || mvm->tcm.data[mac].opened_rx_ba_sessions ||
        mvm->tcm.data[mac].uapsd_nonagg_detect.detected) {
        return;
    }

    if (iwl_mvm_has_new_rx_api(mvm)) {
        tpt = 8 * bytes; /* kbps */
        do_div(tpt, elapsed);
        rate *= 1000; /* kbps */
        if (tpt < 22 * rate / 100) { return; }
    } else {
        /*
         * the rate here is actually the threshold, in 100Kbps units,
         * so do the needed conversion from bytes to 100Kbps:
         * 100kb = bits / (100 * 1000),
         * 100kbps = 100kb / (msecs / 1000) ==
         *           (bits / (100 * 1000)) / (msecs / 1000) ==
         *           bits / (100 * msecs)
         */
        tpt = (8 * bytes);
        do_div(tpt, elapsed * 100);
        if (tpt < rate) { return; }
    }

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_uapsd_agg_disconnect_iter, &mac);
}

static void iwl_mvm_tcm_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
    uint32_t* band = _data;

    if (!mvmvif->phy_ctxt) { return; }

    band[mvmvif->id] = mvmvif->phy_ctxt->channel->band;
}

static unsigned long iwl_mvm_calc_tcm_stats(struct iwl_mvm* mvm, unsigned long ts,
                                            bool handle_uapsd) {
    unsigned int elapsed = jiffies_to_msecs(ts - mvm->tcm.ts);
    unsigned int uapsd_elapsed = jiffies_to_msecs(ts - mvm->tcm.uapsd_nonagg_ts);
    uint32_t total_airtime = 0;
    uint32_t band_airtime[NUM_NL80211_BANDS] = {0};
    uint32_t band[NUM_MAC_INDEX_DRIVER] = {0};
    int ac, mac, i;
    bool low_latency = false;
    enum iwl_mvm_traffic_load load, band_load;
    bool handle_ll = time_after(ts, mvm->tcm.ll_ts + MVM_LL_PERIOD);

    if (handle_ll) { mvm->tcm.ll_ts = ts; }
    if (handle_uapsd) { mvm->tcm.uapsd_nonagg_ts = ts; }

    mvm->tcm.result.elapsed = elapsed;

    ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                               iwl_mvm_tcm_iterator, &band);

    for (mac = 0; mac < NUM_MAC_INDEX_DRIVER; mac++) {
        struct iwl_mvm_tcm_mac* mdata = &mvm->tcm.data[mac];
        uint32_t vo_vi_pkts = 0;
        uint32_t airtime = mdata->rx.airtime + mdata->tx.airtime;

        total_airtime += airtime;
        band_airtime[band[mac]] += airtime;

        load = iwl_mvm_tcm_load(mvm, airtime, elapsed);
        mvm->tcm.result.change[mac] = load != mvm->tcm.result.load[mac];
        mvm->tcm.result.load[mac] = load;
        mvm->tcm.result.airtime[mac] = airtime;

        for (ac = IEEE80211_AC_VO; ac <= IEEE80211_AC_VI; ac++) {
            vo_vi_pkts += mdata->rx.pkts[ac] + mdata->tx.pkts[ac];
        }

        /* enable immediately with enough packets but defer disabling */
        if (vo_vi_pkts > IWL_MVM_TCM_LOWLAT_ENABLE_THRESH) {
            mvm->tcm.result.low_latency[mac] = true;
        } else if (handle_ll) {
            mvm->tcm.result.low_latency[mac] = false;
        }

        if (handle_ll) {
            /* clear old data */
            memset(&mdata->rx.pkts, 0, sizeof(mdata->rx.pkts));
            memset(&mdata->tx.pkts, 0, sizeof(mdata->tx.pkts));
        }
        low_latency |= mvm->tcm.result.low_latency[mac];

        if (!mvm->tcm.result.low_latency[mac] && handle_uapsd) {
            iwl_mvm_check_uapsd_agg_expected_tpt(mvm, uapsd_elapsed, mac);
        }

        /* clear old data */
        memset(&mdata->rx.airtime, 0, sizeof(mdata->rx.airtime));
        memset(&mdata->tx.airtime, 0, sizeof(mdata->tx.airtime));
        if (handle_uapsd) { mdata->uapsd_nonagg_detect.rx_bytes = 0; }
    }

    load = iwl_mvm_tcm_load(mvm, total_airtime, elapsed);
    mvm->tcm.result.global_change = load != mvm->tcm.result.global_load;
    mvm->tcm.result.global_load = load;

    for (i = 0; i < NUM_NL80211_BANDS; i++) {
        band_load = iwl_mvm_tcm_load(mvm, band_airtime[i], elapsed);
        mvm->tcm.result.band_load[i] = band_load;
    }

    /*
     * If the current load isn't low we need to force re-evaluation
     * in the TCM period, so that we can return to low load if there
     * was no traffic at all (and thus iwl_mvm_recalc_tcm didn't get
     * triggered by traffic).
     */
    if (load != IWL_MVM_TRAFFIC_LOW) { return MVM_TCM_PERIOD; }
    /*
     * If low-latency is active we need to force re-evaluation after
     * (the longer) MVM_LL_PERIOD, so that we can disable low-latency
     * when there's no traffic at all.
     */
    if (low_latency) { return MVM_LL_PERIOD; }
    /*
     * Otherwise, we don't need to run the work struct because we're
     * in the default "idle" state - traffic indication is low (which
     * also covers the "no traffic" case) and low-latency is disabled
     * so there's no state that may need to be disabled when there's
     * no traffic at all.
     *
     * Note that this has no impact on the regular scheduling of the
     * updates triggered by traffic - those happen whenever one of the
     * two timeouts expire (if there's traffic at all.)
     */
    return 0;
}

void iwl_mvm_recalc_tcm(struct iwl_mvm* mvm) {
    unsigned long ts = jiffies;
    bool handle_uapsd =
        time_after(ts, mvm->tcm.uapsd_nonagg_ts + msecs_to_jiffies(IWL_MVM_UAPSD_NONAGG_PERIOD));

    spin_lock(&mvm->tcm.lock);
    if (mvm->tcm.paused || !time_after(ts, mvm->tcm.ts + MVM_TCM_PERIOD)) {
        spin_unlock(&mvm->tcm.lock);
        return;
    }
    spin_unlock(&mvm->tcm.lock);

    if (handle_uapsd && iwl_mvm_has_new_rx_api(mvm)) {
        mutex_lock(&mvm->mutex);
        if (iwl_mvm_request_statistics(mvm, true)) { handle_uapsd = false; }
        mutex_unlock(&mvm->mutex);
    }

    spin_lock(&mvm->tcm.lock);
    /* re-check if somebody else won the recheck race */
    if (!mvm->tcm.paused && time_after(ts, mvm->tcm.ts + MVM_TCM_PERIOD)) {
        /* calculate statistics */
        unsigned long work_delay = iwl_mvm_calc_tcm_stats(mvm, ts, handle_uapsd);

        /* the memset needs to be visible before the timestamp */
        smp_mb();
        mvm->tcm.ts = ts;
        if (work_delay) { schedule_delayed_work(&mvm->tcm.work, work_delay); }
    }
    spin_unlock(&mvm->tcm.lock);

    iwl_mvm_tcm_results(mvm);
}

void iwl_mvm_tcm_work(struct work_struct* work) {
    struct delayed_work* delayed_work = to_delayed_work(work);
    struct iwl_mvm* mvm = container_of(delayed_work, struct iwl_mvm, tcm.work);

    iwl_mvm_recalc_tcm(mvm);
}

void iwl_mvm_pause_tcm(struct iwl_mvm* mvm, bool with_cancel) {
    spin_lock_bh(&mvm->tcm.lock);
    mvm->tcm.paused = true;
    spin_unlock_bh(&mvm->tcm.lock);
    if (with_cancel) { cancel_delayed_work_sync(&mvm->tcm.work); }
}

void iwl_mvm_resume_tcm(struct iwl_mvm* mvm) {
    int mac;
    bool low_latency = false;

    spin_lock_bh(&mvm->tcm.lock);
    mvm->tcm.ts = jiffies;
    mvm->tcm.ll_ts = jiffies;
    for (mac = 0; mac < NUM_MAC_INDEX_DRIVER; mac++) {
        struct iwl_mvm_tcm_mac* mdata = &mvm->tcm.data[mac];

        memset(&mdata->rx.pkts, 0, sizeof(mdata->rx.pkts));
        memset(&mdata->tx.pkts, 0, sizeof(mdata->tx.pkts));
        memset(&mdata->rx.airtime, 0, sizeof(mdata->rx.airtime));
        memset(&mdata->tx.airtime, 0, sizeof(mdata->tx.airtime));

        if (mvm->tcm.result.low_latency[mac]) { low_latency = true; }
    }
    /* The TCM data needs to be reset before "paused" flag changes */
    smp_mb();
    mvm->tcm.paused = false;

    /*
     * if the current load is not low or low latency is active, force
     * re-evaluation to cover the case of no traffic.
     */
    if (mvm->tcm.result.global_load > IWL_MVM_TRAFFIC_LOW) {
        schedule_delayed_work(&mvm->tcm.work, MVM_TCM_PERIOD);
    } else if (low_latency) {
        schedule_delayed_work(&mvm->tcm.work, MVM_LL_PERIOD);
    }

    spin_unlock_bh(&mvm->tcm.lock);
}

void iwl_mvm_tcm_add_vif(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

    INIT_DELAYED_WORK(&mvmvif->uapsd_nonagg_detected_wk, iwl_mvm_tcm_uapsd_nonagg_detected_wk);
}

void iwl_mvm_tcm_rm_vif(struct iwl_mvm* mvm, struct ieee80211_vif* vif) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

    cancel_delayed_work_sync(&mvmvif->uapsd_nonagg_detected_wk);
}

void iwl_mvm_get_sync_time(struct iwl_mvm* mvm, uint32_t* gp2, uint64_t* boottime) {
    bool ps_disabled;

    lockdep_assert_held(&mvm->mutex);

    /* Disable power save when reading GP2 */
    ps_disabled = mvm->ps_disabled;
    if (!ps_disabled) {
        mvm->ps_disabled = true;
        iwl_mvm_power_update_device(mvm);
    }

    *gp2 = iwl_read_prph(mvm->trans, DEVICE_SYSTEM_TIME_REG);
    *boottime = ktime_get_boot_ns();

    if (!ps_disabled) {
        mvm->ps_disabled = ps_disabled;
        iwl_mvm_power_update_device(mvm);
    }
}
#endif  // NEEDS_PORTING
