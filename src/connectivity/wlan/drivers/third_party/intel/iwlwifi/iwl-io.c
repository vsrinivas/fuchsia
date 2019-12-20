/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
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

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"

#include <zircon/syscalls.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-fh.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"

void iwl_write8(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {
  iwl_trans_write8(trans, ofs, val);
}

void iwl_write32(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
  iwl_trans_write32(trans, ofs, val);
}

void iwl_write64(struct iwl_trans* trans, uint64_t ofs, uint64_t val) {
  iwl_trans_write32(trans, ofs, lower_32_bits(val));
  iwl_trans_write32(trans, ofs + 4, upper_32_bits(val));
}

uint32_t iwl_read32(struct iwl_trans* trans, uint32_t ofs) {
  uint32_t val = iwl_trans_read32(trans, ofs);

  return val;
}

#define IWL_POLL_INTERVAL ZX_USEC(10)

zx_status_t iwl_poll_bit(struct iwl_trans* trans, uint32_t addr, uint32_t bits, uint32_t mask,
                         int timeout, zx_duration_t* elapsed) {
  zx_status_t ret = ZX_ERR_TIMED_OUT;
  zx_duration_t t = 0;

  do {
    if ((iwl_read32(trans, addr) & mask) == (bits & mask)) {
      ret = ZX_OK;
      break;
    }
    zx_nanosleep(zx_deadline_after(IWL_POLL_INTERVAL));
    t = zx_duration_add_duration(t, IWL_POLL_INTERVAL);
  } while (t < timeout);

  if (elapsed) {
    *elapsed = t;
  }
  return ret;
}

uint32_t iwl_read_direct32(struct iwl_trans* trans, uint32_t reg) {
  uint32_t value = 0x5a5a5a5a;
  unsigned long flags;
  if (iwl_trans_grab_nic_access(trans, &flags)) {
    value = iwl_read32(trans, reg);
    iwl_trans_release_nic_access(trans, &flags);
  }

  return value;
}

void iwl_write_direct32(struct iwl_trans* trans, uint32_t reg, uint32_t value) {
  unsigned long flags;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    iwl_write32(trans, reg, value);
    iwl_trans_release_nic_access(trans, &flags);
  }
}

void iwl_write_direct64(struct iwl_trans* trans, uint64_t reg, uint64_t value) {
  unsigned long flags;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    iwl_write64(trans, reg, value);
    iwl_trans_release_nic_access(trans, &flags);
  }
}

zx_status_t iwl_poll_direct_bit(struct iwl_trans* trans, uint32_t addr, uint32_t mask,
                                int timeout) {
  zx_duration_t t = 0;

  do {
    if ((iwl_read_direct32(trans, addr) & mask) == mask) {
      return t;
    }
    zx_nanosleep(zx_deadline_after(IWL_POLL_INTERVAL));
    t = zx_duration_add_duration(t, IWL_POLL_INTERVAL);
  } while (t < timeout);

  return ZX_ERR_TIMED_OUT;
}

uint32_t iwl_read_prph_no_grab(struct iwl_trans* trans, uint32_t ofs) {
  uint32_t val = iwl_trans_read_prph(trans, ofs);
  return val;
}

void iwl_write_prph_no_grab(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
  iwl_trans_write_prph(trans, ofs, val);
}

void iwl_write_prph64_no_grab(struct iwl_trans* trans, uint64_t ofs, uint64_t val) {
  iwl_write_prph_no_grab(trans, ofs, val & 0xffffffff);
  iwl_write_prph_no_grab(trans, ofs + 4, val >> 32);
}

uint32_t iwl_read_prph(struct iwl_trans* trans, uint32_t ofs) {
  unsigned long flags;
  uint32_t val = 0x5a5a5a5a;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    val = iwl_read_prph_no_grab(trans, ofs);
    iwl_trans_release_nic_access(trans, &flags);
  }
  return val;
}

void iwl_write_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
  unsigned long flags;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    iwl_write_prph_no_grab(trans, ofs, val);
    iwl_trans_release_nic_access(trans, &flags);
  }
}

zx_status_t iwl_poll_prph_bit(struct iwl_trans* trans, uint32_t addr, uint32_t bits, uint32_t mask,
                              int timeout) {
  int t = 0;

  do {
    if ((iwl_read_prph(trans, addr) & mask) == (bits & mask)) {
      return t;
    }
    zx_nanosleep(zx_deadline_after(IWL_POLL_INTERVAL));
    t = zx_duration_add_duration(t, IWL_POLL_INTERVAL);
  } while (t < timeout);

  return ZX_ERR_TIMED_OUT;
}

void iwl_set_bits_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t mask) {
  unsigned long flags;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    iwl_write_prph_no_grab(trans, ofs, iwl_read_prph_no_grab(trans, ofs) | mask);
    iwl_trans_release_nic_access(trans, &flags);
  }
}

void iwl_set_bits_mask_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t bits, uint32_t mask) {
  unsigned long flags;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    iwl_write_prph_no_grab(trans, ofs, (iwl_read_prph_no_grab(trans, ofs) & mask) | bits);
    iwl_trans_release_nic_access(trans, &flags);
  }
}

void iwl_clear_bits_prph(struct iwl_trans* trans, uint32_t ofs, uint32_t mask) {
  unsigned long flags;
  uint32_t val;

  if (iwl_trans_grab_nic_access(trans, &flags)) {
    val = iwl_read_prph_no_grab(trans, ofs);
    iwl_write_prph_no_grab(trans, ofs, (val & ~mask));
    iwl_trans_release_nic_access(trans, &flags);
  }
}

void iwl_force_nmi(struct iwl_trans* trans) {
  if (trans->cfg->device_family < IWL_DEVICE_FAMILY_9000) {
    iwl_write_prph(trans, DEVICE_SET_NMI_REG, DEVICE_SET_NMI_VAL_DRV);
  } else {
    iwl_write_prph(trans, UREG_NIC_SET_NMI_DRIVER, UREG_NIC_SET_NMI_DRIVER_NMI_FROM_DRIVER_MSK);
  }
}

static const char* get_rfh_string(int cmd) {
#define IWL_CMD(x) \
  case x:          \
    return #x
#define IWL_CMD_MQ(arg, reg, q) \
  {                             \
    if (arg == reg(q))          \
      return #reg;              \
  }

  int i;

  for (i = 0; i < IWL_MAX_RX_HW_QUEUES; i++) {
    IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_BA_LSB, i);
    IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_WIDX, i);
    IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_RIDX, i);
    IWL_CMD_MQ(cmd, RFH_Q_URBD_STTS_WPTR_LSB, i);
  }

  switch (cmd) {
    IWL_CMD(RFH_RXF_DMA_CFG);
    IWL_CMD(RFH_GEN_CFG);
    IWL_CMD(RFH_GEN_STATUS);
    IWL_CMD(FH_TSSR_TX_STATUS_REG);
    IWL_CMD(FH_TSSR_TX_ERROR_REG);
    default:
      return "UNKNOWN";
  }
#undef IWL_CMD_MQ
}

struct reg {
  uint32_t addr;
  bool is64;
};

static int iwl_dump_rfh(struct iwl_trans* trans, char** buf) {
  size_t i;
  int q, num_q = trans->num_rx_queues;
  static const uint32_t rfh_tbl[] = {
      RFH_RXF_DMA_CFG, RFH_GEN_CFG, RFH_GEN_STATUS, FH_TSSR_TX_STATUS_REG, FH_TSSR_TX_ERROR_REG,
  };
  static const struct reg rfh_mq_tbl[] = {
      {RFH_Q0_FRBDCB_BA_LSB, true},
      {RFH_Q0_FRBDCB_WIDX, false},
      {RFH_Q0_FRBDCB_RIDX, false},
      {RFH_Q0_URBD_STTS_WPTR_LSB, true},
  };

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  if (buf) {
    int pos = 0;
    /*
     * Register (up to 34 for name + 8 blank/q for MQ): 40 chars
     * Colon + space: 2 characters
     * 0X%08x: 10 characters
     * New line: 1 character
     * Total of 53 characters
     */
    size_t bufsz = ARRAY_SIZE(rfh_tbl) * 53 + ARRAY_SIZE(rfh_mq_tbl) * 53 * num_q + 40;

    *buf = kmalloc(bufsz, GFP_KERNEL);
    if (!*buf) {
      return -ENOMEM;
    }

    pos += scnprintf(*buf + pos, bufsz - pos, "RFH register values:\n");

    for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
      pos += scnprintf(*buf + pos, bufsz - pos, "%40s: 0X%08x\n", get_rfh_string(rfh_tbl[i]),
                       iwl_read_prph(trans, rfh_tbl[i]));

    for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
      for (q = 0; q < num_q; q++) {
        uint32_t addr = rfh_mq_tbl[i].addr;

        addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
        pos += scnprintf(*buf + pos, bufsz - pos, "%34s(q %2d): 0X%08x\n", get_rfh_string(addr), q,
                         iwl_read_prph(trans, addr));
      }

    return pos;
  }
#endif

  IWL_ERR(trans, "RFH register values:\n");
  for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
    IWL_ERR(trans, "  %34s: 0X%08x\n", get_rfh_string(rfh_tbl[i]),
            iwl_read_prph(trans, rfh_tbl[i]));

  for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
    for (q = 0; q < num_q; q++) {
      uint32_t addr = rfh_mq_tbl[i].addr;

      addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
      IWL_ERR(trans, "  %34s(q %d): 0X%08x\n", get_rfh_string(addr), q, iwl_read_prph(trans, addr));
    }

  return 0;
}

static const char* get_fh_string(int cmd) {
  switch (cmd) {
    IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
    IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
    IWL_CMD(FH_RSCSR_CHNL0_WPTR);
    IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
    IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
    IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
    IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
    IWL_CMD(FH_TSSR_TX_STATUS_REG);
    IWL_CMD(FH_TSSR_TX_ERROR_REG);
    default:
      return "UNKNOWN";
  }
#undef IWL_CMD
}

int iwl_dump_fh(struct iwl_trans* trans, char** buf) {
  size_t i;
  static const uint32_t fh_tbl[] = {
      FH_RSCSR_CHNL0_STTS_WPTR_REG,      FH_RSCSR_CHNL0_RBDCB_BASE_REG, FH_RSCSR_CHNL0_WPTR,
      FH_MEM_RCSR_CHNL0_CONFIG_REG,      FH_MEM_RSSR_SHARED_CTRL_REG,   FH_MEM_RSSR_RX_STATUS_REG,
      FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV, FH_TSSR_TX_STATUS_REG,         FH_TSSR_TX_ERROR_REG};

  if (trans->cfg->mq_rx_supported) {
    return iwl_dump_rfh(trans, buf);
  }

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  if (buf) {
    int pos = 0;
    size_t bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;

    *buf = kmalloc(bufsz, GFP_KERNEL);
    if (!*buf) {
      return -ENOMEM;
    }

    pos += scnprintf(*buf + pos, bufsz - pos, "FH register values:\n");

    for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
      pos += scnprintf(*buf + pos, bufsz - pos, "  %34s: 0X%08x\n", get_fh_string(fh_tbl[i]),
                       iwl_read_direct32(trans, fh_tbl[i]));

    return pos;
  }
#endif

  IWL_ERR(trans, "FH register values:\n");
  for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
    IWL_ERR(trans, "  %34s: 0X%08x\n", get_fh_string(fh_tbl[i]),
            iwl_read_direct32(trans, fh_tbl[i]));

  return 0;
}
