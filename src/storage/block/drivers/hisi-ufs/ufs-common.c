// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <hw/reg.h>

#include "ufs.h"

#define INACTIVE_LUN -1
#define BLOCK_OP(op) ((op)&BLOCK_OP_MASK)
#define GET_UFSHC_DEV(dev, lun_idx) (containerof(dev - lun_idx, ufshc_dev_t, lun_blk_devs))

static void ufs_get_cmd(uint8_t opcode, uint32_t lba, uint32_t size, uint8_t* cmd) {
  memset(cmd, 0x0, UPIU_CDB_MAX_LEN);
  switch (opcode) {
    case TEST_UNIT_OPCODE:
      cmd[0] = opcode;
      break;
    case INQUIRY_OPCODE:
      cmd[0] = opcode;
      cmd[3] = (uint8_t)((size & 0xFF00) >> 8);
      cmd[4] = (uint8_t)(size & 0xFF);
      break;
    case READ_CAPA16_OPCODE:
      cmd[0] = opcode;
      cmd[1] = UFS_READ_CAPA16_SACT;
      cmd[13] = (uint8_t)(size & 0xFF);
      break;
    case READ10_OPCODE: {
      uint32_t num_blocks = size / UFS_DEV_SECT_SIZE;
      cmd[0] = opcode;
      cmd[2] = (uint8_t)((lba & 0xFF000000) >> 24);
      cmd[3] = (uint8_t)((lba & 0xFF0000) >> 16);
      cmd[4] = (uint8_t)((lba & 0xFF00) >> 8);
      cmd[5] = (uint8_t)(lba & 0xFF);
      cmd[7] = (uint8_t)((num_blocks & 0xFF00) >> 8);
      cmd[8] = (uint8_t)(num_blocks & 0xFF);
      break;
    }
    default:
      break;
  };
}

zx_status_t ufshc_send_uic_command(volatile void* regs, uint32_t command, uint32_t arg1,
                                   uint32_t arg3) {
  uint32_t reg_val;

  zx_time_t deadline = zx_clock_get_monotonic() + ZX_MSEC(100);
  while (true) {
    if (readl(regs + REG_CONTROLLER_STATUS) & UFS_HCS_UCRDY)
      break;

    if (zx_clock_get_monotonic() > deadline) {
      UFS_ERROR("UFS HC not ready!\n");
      return ZX_ERR_TIMED_OUT;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }

  writel(UFS_IS_UCCS_BIT | UFS_IS_UE_BIT, regs + REG_INTERRUPT_STATUS);
  writel(arg1, regs + REG_UIC_COMMAND_ARG_1);
  writel(0x0, regs + REG_UIC_COMMAND_ARG_2);
  writel(arg3, regs + REG_UIC_COMMAND_ARG_3);
  writel(command & 0xFF, regs + REG_UIC_COMMAND);

  deadline = zx_clock_get_monotonic() + ZX_MSEC(500);
  while (true) {
    if (readl(regs + REG_INTERRUPT_STATUS) & UFS_IS_UCCS_BIT)
      break;

    if (zx_clock_get_monotonic() > deadline) {
      UFS_ERROR("UFS_IS_UCCS_BIT not ready!\n");
      return ZX_ERR_TIMED_OUT;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }

  // clear interrupt status
  writel(UFS_IS_UCCS_BIT, regs + REG_INTERRUPT_STATUS);

  reg_val = readl(regs + REG_UIC_COMMAND_ARG_2) & 0xFF;
  if (reg_val) {
    UFS_ERROR("Response ERROR!\n");
    return ZX_ERR_BAD_STATE;
  }

  reg_val = readl(regs + REG_INTERRUPT_STATUS) & UFS_IS_UE_BIT;
  if (reg_val) {
    UFS_ERROR("UFS_IS_UE_BIT ERROR!\n");
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

uint32_t ufshc_uic_cmd_read(volatile void* regs, uint32_t command, uint32_t arg1) {
  ufshc_send_uic_command(regs, command, arg1, 0);
  // Get UIC result
  return (readl(regs + REG_UIC_COMMAND_ARG_3));
}

void ufshc_check_h8(volatile void* regs) {
  uint32_t tx_fsm_val_0;
  uint32_t tx_fsm_val_1;
  uint32_t i;

  // Unipro VS_mphy_disable
  tx_fsm_val_0 = ufshc_uic_cmd_read(regs, DME_GET, UPRO_MPHY_CTRL);
  if (tx_fsm_val_0 != 0x1)
    UFS_WARN("Unipro VS_mphy_disable is 0x%x!\n", tx_fsm_val_0);

  ufshc_send_uic_command(regs, DME_SET, UPRO_MPHY_CTRL, 0x0);

  for (i = 0; i < MPHY_TX_FSM_RETRY_COUNT; i++) {
    // MPHY TX_FSM_State TX0
    tx_fsm_val_0 = ufshc_uic_cmd_read(regs, DME_GET, UPRO_MPHY_FSM_TX0);
    // MPHY TX_FSM_State TX1
    tx_fsm_val_1 = ufshc_uic_cmd_read(regs, DME_GET, UPRO_MPHY_FSM_TX1);
    if ((tx_fsm_val_0 == 0x1) && (tx_fsm_val_1 == 0x1)) {
      UFS_DBG("tx_fsm_val_0=0x%x tx_fsm_val_1=0x%x.\n", tx_fsm_val_0, tx_fsm_val_1);
      break;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));
  }

  if (i == MPHY_TX_FSM_RETRY_COUNT)
    UFS_WARN("MPHY TX_FSM state wait H8 timeout!\n");
}

void ufshc_disable_auto_h8(volatile void* regs) {
  uint32_t reg_val;

  reg_val = readl(regs + REG_CONTROLLER_AHIT);
  reg_val = reg_val & (~UFS_AHT_AH8ITV_MASK);
  writel(reg_val, regs + REG_CONTROLLER_AHIT);
}

static void ufshc_flush_and_invalidate_descs(ufs_hba_t* hba) {
  io_buffer_cache_flush_invalidate(&hba->utrl_dma_buf, 0, sizeof(utp_tfr_req_desc_t) * hba->nutrs);
  io_buffer_cache_flush_invalidate(&hba->utmrl_dma_buf, 0,
                                   sizeof(utp_task_req_desc_t) * hba->nutmrs);
  io_buffer_cache_flush_invalidate(&hba->ucdl_dma_buf, 0, sizeof(utp_tfr_cmd_desc_t) * hba->nutrs);
}

static void ufs_create_cmd_upiu(ufs_hba_t* hba, uint8_t opcode, enum dma_direction dirn,
                                uint64_t lba, uint32_t size, uint8_t free_slot) {
  ufs_utp_cmd_upiu_t* cmd_upiu;
  utp_tfr_req_desc_t* utrd;
  utp_tfr_cmd_desc_t* ucmd;
  ufshcd_prd_t* prdt;
  zx_paddr_t req_buf_phys;
  uint32_t data_dirn;
  uint8_t upiu_flags;

  ucmd = hba->cmd_desc;
  ucmd += free_slot;
  cmd_upiu = (ufs_utp_cmd_upiu_t*)ucmd->cmd_upiu;
  utrd = hba->tfr_desc;
  utrd += free_slot;

  if (dirn == UFS_DMA_FROM_DEVICE) {
    data_dirn = UTP_DEVICE_TO_HOST;
    upiu_flags = UPIU_CMD_FLAGS_READ;
  } else if (dirn == UFS_DMA_TO_DEVICE) {
    data_dirn = UTP_HOST_TO_DEVICE;
    upiu_flags = UPIU_CMD_FLAGS_WRITE;
  } else {
    data_dirn = UTP_NO_DATA_TFR;
    upiu_flags = UPIU_CMD_FLAGS_NONE;
  }

  utrd->ct_flags = (uint8_t)(data_dirn | UTP_UFS_STORAGE_CMD);
  utrd->resp_upiu_len = htole16((uint16_t)(sizeof(ufs_utp_resp_upiu_t) >> 2));
  utrd->ocs = 0x0f;
  utrd->crypt_en = (uint8_t)0x0;

  cmd_upiu->trans_type = UPIU_TYPE_CMD;
  cmd_upiu->flags = upiu_flags;
  cmd_upiu->lun = hba->active_lun;
  cmd_upiu->task_tag = free_slot;
  cmd_upiu->res1_0 = 0x0;
  cmd_upiu->res1_1 = 0x0;
  cmd_upiu->res1_2 = 0x0;
  cmd_upiu->tot_ehs_len = 0x0;
  cmd_upiu->res2 = 0x0;
  cmd_upiu->data_seg_len = 0x0;
  cmd_upiu->exp_data_xfer_len = htobe32(size);
  ufs_get_cmd(opcode, lba, size, cmd_upiu->cdb);

  utrd->prd_table_len = (uint16_t)((size & (PRDT_BUF_SIZE - 1)) ? ((size / PRDT_BUF_SIZE) + 1)
                                                                : (size / PRDT_BUF_SIZE));

  if (dirn != UFS_DMA_NONE) {
    prdt = hba->lrb_buf[free_slot].prdt;
    req_buf_phys = io_buffer_phys(&hba->req_dma_buf);

    for (uint32_t i = 0; size; i++) {
      prdt[i].base_addr = LOWER_32_BITS(req_buf_phys + (i * PRDT_BUF_SIZE));
      prdt[i].upper_addr = UPPER_32_BITS(req_buf_phys + (i * PRDT_BUF_SIZE));
      prdt[i].res1 = 0x0;
      prdt[i].size = ((PRDT_BUF_SIZE < size) ? PRDT_BUF_SIZE : size) - 1;
      size -= (PRDT_BUF_SIZE < size) ? PRDT_BUF_SIZE : size;
    }
  }

  // Use this transfer slot
  hba->outstanding_xfer_reqs |= (1 << free_slot);
}

static void ufs_create_nop_out_upiu(ufs_hba_t* hba, uint8_t free_slot) {
  ufs_nop_req_upiu_t* cmd_upiu;
  utp_tfr_req_desc_t* utrd;
  uint32_t i;

  utrd = hba->lrb_buf[free_slot].utrd;
  utrd->ct_flags = (uint8_t)(UTP_NO_DATA_TFR | UTP_UFS_STORAGE_CMD);
  utrd->resp_upiu_len = htole16((uint16_t)(sizeof(ufs_nop_resp_upiu_t) >> 2));
  utrd->prd_table_len = 0;
  utrd->ocs = 0xf;

  cmd_upiu = (ufs_nop_req_upiu_t*)(hba->lrb_buf[free_slot].cmd_upiu);
  cmd_upiu->trans_type = UPIU_TYPE_NOP_OUT;
  cmd_upiu->flags = UPIU_CMD_FLAGS_NONE;
  cmd_upiu->res1 = 0x0;
  cmd_upiu->task_tag = free_slot;
  cmd_upiu->res2 = 0x0;
  cmd_upiu->tot_ehs_len = 0x0;
  cmd_upiu->res3 = 0x0;
  cmd_upiu->data_seg_len = 0x0;

  for (i = 0; i < 20; i++) {
    cmd_upiu->res4[i] = 0x0;
  }

  // Use this transfer slot
  hba->outstanding_xfer_reqs |= (1 << free_slot);
  memset(hba->lrb_buf[free_slot].resp_upiu, 0, sizeof(ufs_nop_resp_upiu_t));
}

static void ufs_create_query_upiu(ufs_hba_t* hba, uint8_t opcode, uint8_t query_func, uint8_t sel,
                                  uint8_t flag, uint8_t index, uint16_t len, const uint8_t* ret_val,
                                  uint8_t free_slot) {
  ufs_query_req_upiu_t* query_upiu;
  utp_tfr_req_desc_t* utrd;
  uint32_t i;

  utrd = hba->lrb_buf[free_slot].utrd;
  utrd->ct_flags = (uint8_t)(UTP_NO_DATA_TFR | UTP_UFS_STORAGE_CMD);
  utrd->resp_upiu_len = htole16((uint16_t)(sizeof(ufs_query_req_upiu_t) >> 2));
  utrd->prd_table_len = 0;

  query_upiu = (ufs_query_req_upiu_t*)(hba->lrb_buf[free_slot].cmd_upiu);
  query_upiu->trans_type = UPIU_TYPE_QUERY_REQ;
  query_upiu->flags = UPIU_CMD_FLAGS_NONE;
  query_upiu->res1 = 0x0;
  query_upiu->task_tag = free_slot;
  query_upiu->res2 = 0x0;
  query_upiu->query_func = query_func;
  query_upiu->query_resp = 0x0;
  query_upiu->res3 = 0x0;
  query_upiu->tot_ehs_len = 0x0;
  query_upiu->data_seg_len = 0x0;
  query_upiu->tsf[0] = opcode;
  query_upiu->tsf[1] = flag;
  query_upiu->tsf[2] = index;
  query_upiu->tsf[3] = sel;
  query_upiu->tsf[4] = 0x0;
  query_upiu->tsf[5] = 0x0;
  query_upiu->tsf[6] = len & 0xff;
  query_upiu->tsf[7] = (uint8_t)(len >> 8);

  // Value or Flag update
  query_upiu->tsf[8] = ret_val[3];
  query_upiu->tsf[9] = ret_val[2];
  query_upiu->tsf[10] = ret_val[1];
  query_upiu->tsf[11] = ret_val[0];

  for (i = 12; i < 15; i++) {
    query_upiu->tsf[i] = 0x0;
  }
  query_upiu->res5 = 0x0;

  // Use this transfer slot
  hba->outstanding_xfer_reqs |= (1 << free_slot);
}

static zx_status_t ufshc_wait_for_active(volatile void* regs, const uint32_t mask,
                                         zx_time_t timeout) {
  zx_time_t deadline = zx_clock_get_monotonic() + timeout;

  while (true) {
    uint32_t reg_value = readl(regs + REG_CONTROLLER_ENABLE);
    if ((reg_value & mask) == 1) {
      UFS_DBG("UFS HC controller is active.\n");
      break;
    }
    UFS_DBG("UFS HC CTRL_EN=0x%x.\n", reg_value);

    if (zx_clock_get_monotonic() > deadline) {
      UFS_ERROR("UFS HC: timed out while waiting for reset!\n");
      return ZX_ERR_TIMED_OUT;
    }
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
  }

  return ZX_OK;
}

static zx_status_t ufshc_pre_link_startup(ufs_hba_t* hba, volatile void* regs) {
  if (hba && hba->vops && hba->vops->link_startup) {
    return hba->vops->link_startup(regs, PRE_CHANGE);
  }

  return ZX_OK;
}

static zx_status_t ufshc_post_link_startup(ufs_hba_t* hba, volatile void* regs) {
  if (hba && hba->vops && hba->vops->link_startup) {
    return hba->vops->link_startup(regs, POST_CHANGE);
  }

  return ZX_OK;
}

static inline void ufshc_reg_read_clear(volatile void* regs) {
  readl(regs + REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
  // DME Error PA Ind
  ufshc_uic_cmd_read(regs, DME_GET, UPRO_ERR_PA_IND);
}

static zx_status_t ufshc_link_startup(volatile void* regs) {
  int32_t retry = 4;
  uint32_t i;

  writel(0xFFFFFFFF, regs + REG_INTERRUPT_STATUS);
  while (retry-- > 0) {
    if (readl(regs + REG_INTERRUPT_STATUS) & UFS_IS_UCCS_BIT)
      writel(UFS_IS_UCCS_BIT, regs + REG_INTERRUPT_STATUS);

    // UFS link startup begin
    writel(0, regs + REG_UIC_COMMAND_ARG_1);
    writel(0, regs + REG_UIC_COMMAND_ARG_2);
    writel(0, regs + REG_UIC_COMMAND_ARG_3);
    writel(UIC_LINK_STARTUP_CMD & 0xFF, regs + REG_UIC_COMMAND);

    for (i = 0; i <= LINK_STARTUP_UCCS_RETRY_COUNT; i++) {
      if (readl(regs + REG_INTERRUPT_STATUS) & UFS_IS_UCCS_BIT) {
        writel(UFS_IS_UCCS_BIT, regs + REG_INTERRUPT_STATUS);
        UFS_DBG("UFS HC Link INT status OK.\n");
        break;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));
    }

    if (readl(regs + REG_CONTROLLER_STATUS) & UFS_HCS_DP_BIT) {
      writel(UFS_IS_UE_BIT, regs + REG_INTERRUPT_STATUS);
      if (readl(regs + REG_CONTROLLER_STATUS) & UFS_IS_ULSS_BIT)
        writel(UFS_IS_ULSS_BIT, regs + REG_INTERRUPT_STATUS);
      UFS_DBG("UFS HC link_startup startup OK.\n");

      ufshc_reg_read_clear(regs);
      return ZX_OK;
    }
  }
  UFS_ERROR("UFS HC link_startup startup Error!\n");

  return ZX_ERR_TIMED_OUT;
}

static zx_status_t ufs_request_alloc(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  zx_status_t status;

  // Allocate memory for UFS data request buffer
  status =
      io_buffer_init(&hba->req_dma_buf, dev->bti, DATA_REQ_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to allocate request buffer!\n");
    return status;
  }
  hba->req_buf = io_buffer_virt(&hba->req_dma_buf);
  memset(hba->req_buf, 0, DATA_REQ_SIZE);

  return ZX_OK;
}

static zx_status_t ufshc_memory_alloc(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  uint32_t utmrl_size, utrl_size, ucdl_size, lrb_size;
  zx_status_t status;

  // Allocate memory for UTP command descriptors
  ucdl_size = (sizeof(utp_tfr_cmd_desc_t) * hba->nutrs);
  status = io_buffer_init(&hba->ucdl_dma_buf, dev->bti, ucdl_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to allocate dma descriptors!\n");
    return status;
  }
  hba->cmd_desc = io_buffer_virt(&hba->ucdl_dma_buf);
  memset(hba->cmd_desc, 0, hba->ucdl_dma_buf.size);

  /*
   * Allocate memory for UTP Transfer descriptors
   * UFSHCI requires 1024 byte alignment of UTRD.
   * io_buffer_init will align to 4K.
   */
  utrl_size = (sizeof(utp_tfr_req_desc_t) * hba->nutrs);
  status = io_buffer_init(&hba->utrl_dma_buf, dev->bti, utrl_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to allocate dma descriptors!\n");
    return status;
  }
  hba->tfr_desc = io_buffer_virt(&hba->utrl_dma_buf);
  memset(hba->tfr_desc, 0, hba->utrl_dma_buf.size);

  /*
   * Allocate memory for UTP Task Management descriptors
   * UFSHCI requires 1024 byte alignment of UTMRD
   * io_buffer_init will align to 4K.
   */
  utmrl_size = (sizeof(utp_task_req_desc_t) * hba->nutmrs);
  status =
      io_buffer_init(&hba->utmrl_dma_buf, dev->bti, utmrl_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to allocate dma descriptors!\n");
    return status;
  }
  hba->req_desc = io_buffer_virt(&hba->utmrl_dma_buf);
  memset(hba->req_desc, 0, hba->utmrl_dma_buf.size);

  // Allocate memory for local reference block
  lrb_size = (sizeof(ufs_hcd_lrb_t) * hba->nutrs);

  hba->lrb_buf = calloc(1, lrb_size);
  if (!hba->lrb_buf) {
    UFS_ERROR("Failed to allocate LRB!\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = ufs_request_alloc(dev);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to allocate request buffer!\n");
    return status;
  }

  return ZX_OK;
}

static void ufshc_memory_configure(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  utp_tfr_req_desc_t* utrdl_desc;
  utp_tfr_cmd_desc_t* ucmd_desc;
  zx_paddr_t ucmd_desc_addr;
  zx_paddr_t ucmd_desc_element_addr;
  uint32_t resp_upiu_len;
  uint32_t resp_upiu_offset;
  uint32_t prdt_offset;
  uint32_t ucmd_desc_size;
  uint32_t i;

  utrdl_desc = hba->tfr_desc;
  ucmd_desc = hba->cmd_desc;

  resp_upiu_offset = (uint32_t)offsetof(utp_tfr_cmd_desc_t, resp_upiu);
  resp_upiu_len = ALIGNED_UPIU_SIZE;
  prdt_offset = (uint32_t)offsetof(utp_tfr_cmd_desc_t, prd_table);

  ucmd_desc_size = sizeof(utp_tfr_cmd_desc_t);
  ucmd_desc_addr = io_buffer_phys(&hba->ucdl_dma_buf);

  for (i = 0; i < hba->nutrs; i++) {
    // Configure UTRD with command descriptor base address
    ucmd_desc_element_addr = (ucmd_desc_addr + (ucmd_desc_size * i));
    utrdl_desc[i].ucdba = htole32(LOWER_32_BITS(ucmd_desc_element_addr));
    utrdl_desc[i].ucdbau = htole32(UPPER_32_BITS(ucmd_desc_element_addr));

    // Response upiu and prdt offset should be in double words
    utrdl_desc[i].resp_upiu_off = htole16((uint16_t)(resp_upiu_offset >> 2));
    utrdl_desc[i].resp_upiu_len = htole16((uint16_t)(resp_upiu_len >> 2));

    utrdl_desc[i].prd_table_off = htole16((uint16_t)(prdt_offset >> 2));
    utrdl_desc[i].prd_table_len = 0;
    hba->lrb_buf[i].utrd = (utrdl_desc + i);
    hba->lrb_buf[i].cmd_upiu = (ufs_utp_cmd_upiu_t*)(ucmd_desc + i);
    hba->lrb_buf[i].resp_upiu = (ufs_utp_resp_upiu_t*)ucmd_desc[i].resp_upiu;
    hba->lrb_buf[i].prdt = (ufshcd_prd_t*)ucmd_desc[i].prd_table;
  }
}

static zx_status_t ufshc_configure_descs(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  volatile void* ufshc_regs = dev->ufshc_mmio.vaddr;
  zx_paddr_t tfr_desc_phys = io_buffer_phys(&hba->utrl_dma_buf);
  zx_paddr_t req_desc_phys = io_buffer_phys(&hba->utmrl_dma_buf);
  zx_status_t status;

  if ((status = ufshc_wait_for_active(ufshc_regs, CONTROLLER_ENABLE, ZX_SEC(1))) != ZX_OK) {
    UFS_ERROR("UFS Host controller not active!\n");
    return status;
  }

  // Configure UTRL and UTMRL base addr registers
  writel(LOWER_32_BITS(tfr_desc_phys), ufshc_regs + REG_UTP_TRANSFER_REQ_LIST_BASE_L);
  writel(UPPER_32_BITS(tfr_desc_phys), ufshc_regs + REG_UTP_TRANSFER_REQ_LIST_BASE_H);

  writel(LOWER_32_BITS(req_desc_phys), ufshc_regs + REG_UTP_TASK_REQ_LIST_BASE_L);
  writel(UPPER_32_BITS(req_desc_phys), ufshc_regs + REG_UTP_TASK_REQ_LIST_BASE_H);

  writel(UFS_UTP_RUN_BIT, ufshc_regs + REG_UTP_TRANSFER_REQ_LIST_RUN_STOP);
  writel(UFS_UTP_RUN_BIT, ufshc_regs + REG_UTP_TASK_REQ_LIST_RUN_STOP);

  // Enable auto H8
  writel(UFS_AHT_AH8ITV_MASK, ufshc_regs + REG_CONTROLLER_AHIT);

  return status;
}

static zx_status_t ufshc_drv_init(ufshc_dev_t* dev) {
  zx_status_t status;
  ufs_hba_t* hba = &dev->ufs_hba;
  volatile void* regs = dev->ufshc_mmio.vaddr;

  // Allocate memory for host memory space
  status = ufshc_memory_alloc(dev);
  if (status)
    return status;

  // Configure LRB
  ufshc_memory_configure(dev);

  status = ufshc_post_link_startup(hba, regs);
  if (status)
    return status;

  // Configure UFS HC descriptors
  status = ufshc_configure_descs(dev);

  return status;
}

static uint8_t ufshc_get_xfer_free_slot(ufs_hba_t* hba) {
  int32_t free_slot;

  free_slot = find_first_zero_bit(&hba->outstanding_xfer_reqs, hba->nutrs);
  if (-1 == free_slot) {
    UFS_ERROR("UFS no free transfer slot available.\n");
    return BAD_SLOT;
  }

  return (uint8_t)free_slot;
}

static zx_status_t ufshc_wait_for_cmd_completion(ufs_hba_t* hba, uint32_t free_slot_mask,
                                                 volatile void* regs) {
  zx_time_t timeout = hba->timeout;
  zx_time_t deadline = zx_clock_get_monotonic() + timeout;
  uint32_t reg_val;

  writel(free_slot_mask, regs + REG_UTP_TRANSFER_REQ_DOOR_BELL);

  // Wait for Doorbell to clear
  for (;;) {
    reg_val = readl(regs + REG_UTP_TRANSFER_REQ_DOOR_BELL);
    if ((reg_val & free_slot_mask) == 0)
      break;
    if (zx_clock_get_monotonic() > deadline) {
      reg_val = readl(regs + REG_UTP_TRANSFER_REQ_DOOR_BELL);
      UFS_ERROR("UTRD Doorbell timeout: 0x%x for slot#0x%x \n", reg_val, free_slot_mask);
      writel(~free_slot_mask, regs + REG_UTP_TRANSFER_REQ_DOOR_BELL);

      // Release xfer request
      hba->outstanding_xfer_reqs &= ~free_slot_mask;
      return UFS_UTRD_DOORBELL_TIMEOUT;
    }
  }

  return ZX_OK;
}

static zx_status_t ufs_handle_scsi_completion(ufs_hba_t* hba) {
  utp_tfr_req_desc_t* utrd;
  ufs_utp_resp_upiu_t* resp_upiu;
  zx_status_t status = ZX_OK;
  uint8_t resp_status;
  uint8_t slot_idx;

  for (slot_idx = 0; slot_idx < hba->nutrs; slot_idx++) {
    if (hba->outstanding_xfer_reqs & UFS_BIT(slot_idx)) {
      resp_upiu = hba->lrb_buf[slot_idx].resp_upiu;
      utrd = hba->lrb_buf[slot_idx].utrd;

      // Release xfer request
      hba->outstanding_xfer_reqs &= ~(1 << slot_idx);
      if (utrd->ocs == 0x0) {
        resp_status = resp_upiu->status;
        if (resp_status == SCSI_CMD_STATUS_CHK_COND) {
          UFS_DBG("Resp Fail! Check condition!\n");
          status = UPIU_RESP_COND_FAIL;
          // TODO: Read error information from sense data
          // We do not return here so as to continue reading all slots.
        } else if (resp_status != SCSI_CMD_STATUS_GOOD) {
          UFS_DBG("Resp Fail! resp_status=0x%x\n", resp_status);
          status = UPIU_RESP_STAT_FAIL;
          // TODO: Read error information from sense data
        }
      } else {
        UFS_DBG("Resp Fail! utrd->ocs=0x%x\n", utrd->ocs);
        status = ZX_ERR_BAD_STATE;
      }
    }
  }

  return status;
}

static int32_t ufs_read_query_resp(ufs_hba_t* hba, uint8_t* ret_val, uint8_t free_slot) {
  ufs_query_req_upiu_t* resp_upiu;

  resp_upiu = (ufs_query_req_upiu_t*)(hba->lrb_buf[free_slot].resp_upiu);

  // Update the ret value
  if (ret_val) {
    ret_val[0] = resp_upiu->tsf[11];
    ret_val[1] = resp_upiu->tsf[10];
    ret_val[2] = resp_upiu->tsf[9];
    ret_val[3] = resp_upiu->tsf[8];
  }

  // Release xfer request
  hba->outstanding_xfer_reqs &= ~(1 << free_slot);

  if (resp_upiu->query_resp == 0x0)
    return ZX_OK;

  UFS_ERROR("Query response error! resp_upiu->query_resp=0x%x\n", resp_upiu->query_resp);
  return -(resp_upiu->query_resp);
}

static int32_t ufshc_read_nop_resp(ufs_hba_t* hba, uint8_t free_slot) {
  ufs_nop_resp_upiu_t* resp_upiu;
  utp_tfr_req_desc_t* utrd;

  resp_upiu = (ufs_nop_resp_upiu_t*)(hba->lrb_buf[free_slot].resp_upiu);
  utrd = hba->lrb_buf[free_slot].utrd;

  // Release xfer request
  hba->outstanding_xfer_reqs &= ~(1 << free_slot);
  if (utrd->ocs != 0x0) {
    UFS_DBG("Send nop out ocs error! utrd->ocs=0x%x.\n", utrd->ocs);
    return UFS_NOP_OUT_OCS_FAIL;
  }

  if ((resp_upiu->trans_type & UPIU_TYPE_REJECT) != UPIU_TYPE_NOP_IN) {
    UFS_DBG("Invalid NOP IN!\n");
    return UFS_INVALID_NOP_IN;
  }

  if (resp_upiu->resp != 0x0) {
    UFS_DBG("NOP IN response err, resp = 0x%x.\n", resp_upiu->resp);
    return UFS_NOP_RESP_FAIL;
  }

  return 0;
}

static inline zx_status_t ufs_get_query_func(uint8_t opcode, uint8_t* query_func) {
  switch (opcode) {
    case SET_FLAG_OPCODE:
      *query_func = STANDARD_WR_REQ;
      break;
    case READ_FLAG_OPCODE:
    case READ_DESC_OPCODE:
      *query_func = STANDARD_RD_REQ;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
      break;
  };

  return ZX_OK;
}

static zx_status_t ufshc_query_dev_desc(ufshc_dev_t* dev, uint8_t opcode, uint8_t desc_idn,
                                        uint8_t desc_idx, void** desc_buf, uint16_t* desc_len) {
  uint8_t ret_val[4];
  uint8_t query_func = 0;
  uint8_t free_slot;
  ufs_utp_resp_upiu_t* resp_upiu;
  zx_status_t status;
  ufs_hba_t* hba = &dev->ufs_hba;
  volatile void* regs = dev->ufshc_mmio.vaddr;
  uint8_t* tmp_buf;

  status = ufs_get_query_func(opcode, &query_func);
  if (status != ZX_OK)
    return status;

  for (uint32_t i = 0; i < 4; i++) {
    ret_val[i] = 0x0;
  }

  free_slot = ufshc_get_xfer_free_slot(hba);
  if (BAD_SLOT == free_slot)
    return ZX_ERR_NO_RESOURCES;

  resp_upiu = hba->lrb_buf[free_slot].resp_upiu;
  ufs_create_query_upiu(hba, opcode, query_func, 0, desc_idn, desc_idx, *desc_len, &ret_val[0],
                        free_slot);

  // Flush and invalidate cache before we start transfer
  ufshc_flush_and_invalidate_descs(hba);

  status = ufshc_wait_for_cmd_completion(hba, (1 << free_slot), regs);
  if (status != ZX_OK) {
    UFS_ERROR("UFS Query Descriptor fail!\n");
    return status;
  }

  status = ufs_read_query_resp(hba, ret_val, free_slot);
  if (status != ZX_OK)
    return status;

  // Fill the response desc buffer and length.
  *((uint8_t**)desc_buf) = (void*)resp_upiu;
  tmp_buf = (uint8_t*)resp_upiu;
  *desc_len = tmp_buf[UFS_UPIU_REQ_HDR_LEN + UFS_RESP_LEN_OFF_L] |
              tmp_buf[UFS_UPIU_REQ_HDR_LEN + UFS_RESP_LEN_OFF_H] << 8;

  return status;
}

static zx_status_t ufs_get_desc_len(ufshc_dev_t* dev, uint8_t desc_idn, uint16_t* desc_len) {
  zx_status_t status;
  void* resp_upiu;

  status = ufshc_query_dev_desc(dev, READ_DESC_OPCODE, desc_idn, 0, &resp_upiu, desc_len);
  if (status != ZX_OK)
    return status;

  UFS_DBG("UFS device descriptor length is 0x%x\n", *desc_len);
  return ZX_OK;
}

static zx_status_t ufs_read_dev_desc(ufshc_dev_t* dev, void** resp_upiu) {
  zx_status_t status;
  uint16_t len = UFS_READ_DESC_MIN_LEN;

  // Get the Device descriptor length first
  status = ufs_get_desc_len(dev, DEVICE_DESC_IDN, &len);
  if (status != ZX_OK) {
    UFS_ERROR("Get DEVICE_DESC Length Fail!\n");
    return status;
  }

  status = ufshc_query_dev_desc(dev, READ_DESC_OPCODE, DEVICE_DESC_IDN, 0, resp_upiu, &len);
  if (status != ZX_OK) {
    UFS_ERROR("Query DEVICE_DESC Fail!\n");
    return status;
  }

  return ZX_OK;
}

static void ufs_update_num_lun(ufs_query_req_upiu_t* resp_upiu, ufs_hba_t* hba) {
  uint8_t* data_ptr;

  // Response UPIU buffer has size of ALLIGNED_UPIU_SIZE bytes
  // allocated in UFS command descriptor
  data_ptr = (uint8_t*)resp_upiu;
  // Skip the query request header to read the response data.
  data_ptr += sizeof(ufs_query_req_upiu_t);

  hba->num_lun = data_ptr[UFS_DEV_DESC_NUM_LUNS];
  UFS_DBG("UFS Number of LUN=%d\n", hba->num_lun);
}

static void ufs_fill_manf_id(ufs_query_req_upiu_t* resp_upiu, ufs_hba_t* hba) {
  uint8_t* data_ptr;

  // Response UPIU buffer has size of ALLIGNED_UPIU_SIZE bytes
  // allocated in UFS command descriptor
  data_ptr = (uint8_t*)resp_upiu;
  // Skip the query request header to read the response data.
  data_ptr += sizeof(ufs_query_req_upiu_t);

  hba->manufacturer_id = data_ptr[UFS_DEV_DESC_MANF_ID_H] << 8 | data_ptr[UFS_DEV_DESC_MANF_ID_L];
  UFS_DBG("Found UFS device. Manf_ID=0x%x.\n", hba->manufacturer_id);
}

static zx_status_t ufshc_get_device_info(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  void* resp_upiu;
  zx_status_t status;

  status = ufs_read_dev_desc(dev, &resp_upiu);
  if (status != ZX_OK)
    return status;

  ufs_update_num_lun(resp_upiu, hba);
  ufs_fill_manf_id(resp_upiu, hba);

  return ZX_OK;
}

static zx_status_t ufshc_send_nop_out_cmd(ufs_hba_t* hba, volatile void* regs) {
  uint32_t i;
  uint8_t free_slot;
  zx_status_t status = ZX_OK;

  for (i = 0; i < NOP_RETRY_COUNT; i++) {
    free_slot = ufshc_get_xfer_free_slot(hba);
    if (BAD_SLOT == free_slot)
      return ZX_ERR_NO_RESOURCES;

    ufs_create_nop_out_upiu(hba, free_slot);

    // Flush and invalidate cache before we start transfer
    ufshc_flush_and_invalidate_descs(hba);

    status = ufshc_wait_for_cmd_completion(hba, (1 << free_slot), regs);
    if (status == ZX_OK) {
      if ((status = ufshc_read_nop_resp(hba, free_slot)) == ZX_OK)
        break;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  }

  if (i == NOP_RETRY_COUNT)
    UFS_ERROR("UFS NOP resposne FAIL! slot=0x%x status=%d.\n", free_slot, status);

  return status;
}

static zx_status_t ufshc_do_flag_opn(ufshc_dev_t* dev, uint8_t opcode, uint8_t flag,
                                     uint8_t* flag_res) {
  volatile void* regs = dev->ufshc_mmio.vaddr;
  ufs_hba_t* hba = &dev->ufs_hba;
  uint8_t ret_val[4];
  uint8_t free_slot;
  uint8_t query_func = 0;
  int32_t i;
  zx_status_t status;

  status = ufs_get_query_func(opcode, &query_func);
  if (status != ZX_OK)
    return status;

  if ((query_func == STANDARD_RD_REQ) && !flag_res) {
    UFS_ERROR("Flag result ptr cannot be NULL!\n");
    return ZX_ERR_INVALID_ARGS;
  }

  for (i = 0; i < 4; i++)
    ret_val[i] = 0x0;

  free_slot = ufshc_get_xfer_free_slot(hba);
  if (BAD_SLOT == free_slot)
    return ZX_ERR_NO_RESOURCES;

  ufs_create_query_upiu(hba, opcode, query_func, 0, flag, 0, 0, &ret_val[0], free_slot);

  // Flush and invalidate cache before we start transfer
  ufshc_flush_and_invalidate_descs(hba);

  status = ufshc_wait_for_cmd_completion(hba, (1 << free_slot), regs);
  if (status != ZX_OK) {
    UFS_ERROR("UFS query response fail for slot=0x%x.\n", free_slot);
    return status;
  }

  status = ufs_read_query_resp(hba, ret_val, free_slot);
  if (flag_res)
    *flag_res = ret_val[0];

  return status;
}

static zx_status_t ufshc_complete_dev_init(ufshc_dev_t* dev) {
  zx_status_t status;
  uint8_t flag_res = 1;

  // Set the Device init flag
  status = ufshc_do_flag_opn(dev, SET_FLAG_OPCODE, FLAG_ID_FDEVICE_INIT, NULL);
  if (status != ZX_OK) {
    UFS_ERROR("UFS set device init flag FAIL!\n");
    return status;
  }

  // Verify if Device Init is success
  status = ufshc_do_flag_opn(dev, READ_FLAG_OPCODE, FLAG_ID_FDEVICE_INIT, &flag_res);
  if (status != ZX_OK || flag_res != 0) {
    UFS_ERROR("UFS device init FAIL!\n");
    return ZX_ERR_BAD_STATE;
  }

  return status;
}

static zx_status_t ufshc_device_init(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;
  volatile void* regs = dev->ufshc_mmio.vaddr;
  zx_status_t status;

  status = ufshc_send_nop_out_cmd(hba, regs);
  if (status != ZX_OK)
    return status;

  status = ufshc_complete_dev_init(dev);
  return status;
}

static void ufshc_config_init(ufs_hba_t* ufs_hba) { ufs_hba->timeout = ZX_SEC(5); /* 5 seconds */ }

static zx_status_t ufshc_enable(ufshc_dev_t* dev) {
  int32_t retry = 3;
  volatile void* regs = dev->ufshc_mmio.vaddr;
  zx_status_t status = ZX_OK;

  do {
    writel(CONTROLLER_ENABLE, regs + REG_CONTROLLER_ENABLE);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    // wait for the host controller to complete initialization
    if ((status = ufshc_wait_for_active(regs, CONTROLLER_ENABLE, ZX_SEC(1))) == ZX_OK)
      break;
  } while (--retry > 0);

  if (!retry)
    UFS_ERROR("Controller not active status=%d.\n", status);

  return status;
}

static inline void ufshc_read_capabilities(ufs_hba_t* hba, volatile void* regs) {
  hba->caps = readl(regs + REG_CONTROLLER_CAPABILITIES);

  // nutrs and nutmrs are 0 based values
  hba->nutrs = (hba->caps & MASK_TRANSFER_REQUESTS_SLOTS) + 1;
  hba->nutmrs = ((hba->caps & MASK_TASK_MANAGEMENT_REQUEST_SLOTS) >> UFS_NUTMRS_SHIFT) + 1;
  UFS_DBG("ufshcd_capabilities hba->nutrs=%d hba->nutmrs=%d.\n", hba->nutrs, hba->nutmrs);
}

static inline void ufshc_get_ufs_version(ufs_hba_t* hba, volatile void* regs) {
  hba->ufs_version = readl(regs + REG_UFS_VERSION);
  UFS_DBG("hba->ufs_version=%u.\n", hba->ufs_version);
}

static zx_status_t ufshc_host_init(ufshc_dev_t* dev) {
  volatile void* regs = dev->ufshc_mmio.vaddr;
  ufs_hba_t* hba = &dev->ufs_hba;
  zx_status_t status;

  // Read capabilities registers
  ufshc_read_capabilities(hba, regs);

  // Get UFS version supported by the controller
  ufshc_get_ufs_version(hba, regs);

  status = ufshc_pre_link_startup(hba, regs);
  if (status != ZX_OK)
    return status;

  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  status = ufshc_link_startup(regs);
  if (status != ZX_OK)
    return status;

  status = ufshc_drv_init(dev);
  return status;
}

static void ufshc_release(ufshc_dev_t* dev) {
  ufs_hba_t* hba = &dev->ufs_hba;

  if (hba->lrb_buf)
    free(hba->lrb_buf);

  io_buffer_release(&hba->ucdl_dma_buf);
  io_buffer_release(&hba->utrl_dma_buf);
  io_buffer_release(&hba->utmrl_dma_buf);
}

static zx_status_t ufs_send_scsi_cmd(ufshc_dev_t* dev, uint8_t lun, uint8_t opcode, uint64_t lba,
                                     enum dma_direction dirn, uint32_t size) {
  ufs_hba_t* hba = &dev->ufs_hba;
  volatile void* regs = dev->ufshc_mmio.vaddr;
  zx_status_t status;
  uint8_t free_slot;

  free_slot = ufshc_get_xfer_free_slot(hba);
  if (BAD_SLOT == free_slot)
    return ZX_ERR_NO_RESOURCES;

  memset(hba->req_buf, 0, size);
  hba->active_lun = lun;
  ufs_create_cmd_upiu(hba, opcode, dirn, lba, size, free_slot);

  // Flush and invalidate cache before we start transfer
  ufshc_flush_and_invalidate_descs(hba);
  io_buffer_cache_flush_invalidate(&hba->req_dma_buf, 0, DATA_REQ_SIZE);

  status = ufshc_wait_for_cmd_completion(hba, 1 << free_slot, regs);
  if (status != ZX_OK)
    return status;

  status = ufs_handle_scsi_completion(hba);
  if (status != ZX_OK)
    return status;

  return status;
}

static zx_status_t ufs_send_inquiry(ufshc_dev_t* dev, uint8_t lun) {
  zx_status_t status;
  uint8_t* cdb_data_buf;
  ufs_hba_t* hba = &dev->ufs_hba;

  status = ufs_send_scsi_cmd(dev, lun, INQUIRY_OPCODE, 0, UFS_DMA_FROM_DEVICE, UFS_INQUIRY_TFR_LEN);
  if (status != ZX_OK)
    return status;

  cdb_data_buf = hba->req_buf;
  UFS_DBG("UFS device vendor:%s model:%s\n", cdb_data_buf + UFS_INQUIRY_VENDOR_OFF,
          cdb_data_buf + UFS_INQUIRY_MODEL_OFF);
  dbg_dump_buffer(cdb_data_buf, UFS_INQUIRY_TFR_LEN, "inquiry");

  return ZX_OK;
}

static zx_status_t ufs_check_lun_ready(ufshc_dev_t* dev, uint8_t lun) {
  return (ufs_send_scsi_cmd(dev, lun, TEST_UNIT_OPCODE, 0, UFS_DMA_NONE, 0x0));
}

static zx_status_t ufs_read_lun_capacity(ufshc_dev_t* dev, uint8_t lun) {
  return (
      ufs_send_scsi_cmd(dev, lun, READ_CAPA16_OPCODE, 0, UFS_DMA_FROM_DEVICE, UFS_READ_CAPA16_LEN));
}

static zx_off_t ufs_lun_get_size(void* ctx) {
  ufs_lun_blk_dev_t* dev = ctx;
  return dev->block_info.block_count * dev->block_info.block_size;
}

static void ufs_lun_blk_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
  ufs_lun_blk_dev_t* dev = ctx;

  memcpy(info_out, &dev->block_info, sizeof(*info_out));
  *block_op_size_out = sizeof(block_op_t);
}

static void ufs_lun_blk_queue(void* ctx, block_op_t* btxn, block_impl_queue_callback completion_cb,
                              void* cookie) {
  ufs_lun_blk_dev_t* dev = ctx;
  ufshc_dev_t* ufshc_dev = GET_UFSHC_DEV(dev, dev->lun_id);
  uint32_t block_size = dev->block_info.block_size;
  uint32_t tfr_size = btxn->rw.length * block_size;
  zx_status_t status = ZX_OK;
  uint8_t opcode;

  switch (BLOCK_OP(btxn->command)) {
    case BLOCK_OP_READ: {
      opcode = READ10_OPCODE;
      UFS_DBG(
          "block_cmd:0x%x offset_dev:0x%lx length:0x%x blocksize:0x%x "
          "max_transfer_size:0x%x\n",
          btxn->command, btxn->rw.offset_dev, btxn->rw.length, dev->block_info.block_size,
          dev->block_info.max_transfer_size);

      status = ufs_send_scsi_cmd(ufshc_dev, dev->lun_id, opcode, btxn->rw.offset_dev,
                                 UFS_DMA_FROM_DEVICE, tfr_size);
      if (status == ZX_OK) {
        uint8_t* data = ufshc_dev->ufs_hba.req_buf;
        status = zx_vmo_write(btxn->rw.vmo, data, btxn->rw.offset_vmo * block_size, tfr_size);
      } else {
        UFS_DBG("ufs_send_scsi_cmd fail! status=%d\n", status);
        completion_cb(cookie, status, btxn);
        return;
      }
      break;
    }
    case BLOCK_OP_WRITE: {
      uint64_t max = dev->block_info.block_count;

      if ((btxn->rw.offset_dev >= max) || ((max - btxn->rw.offset_dev) < btxn->rw.length)) {
        UFS_DBG("BLOCK_OP_RD_WRITE- Out of Range!\n");
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, btxn);
        return;
      }

      if (btxn->rw.length == 0) {
        UFS_DBG("BLOCK_OP_RD_WRITE- Len=0.\n");
        completion_cb(cookie, ZX_OK, btxn);
        return;
      }
      break;
      // TODO: Implement SCSI cmd Write
    }
    case BLOCK_OP_FLUSH:
      UFS_DBG("BLOCK_OP_FLUSH \n");
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, btxn);
      return;
  }

  completion_cb(cookie, status, btxn);
}

static zx_protocol_device_t ufs_lun_dev_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_size = ufs_lun_get_size,
};

static block_impl_protocol_ops_t ufs_lun_blk_ops = {
    .query = ufs_lun_blk_query,
    .queue = ufs_lun_blk_queue,
};

static zx_status_t ufs_add_lun_blk_dev(ufshc_dev_t* dev, uint8_t lun) {
  char disk_name[20] = {0};
  zx_status_t status;
  ufs_hba_t* hba = &dev->ufs_hba;
  ufs_readcapa16_data_t* rd_capa16_buf;
  ufs_lun_blk_dev_t* lun_blk_dev = &dev->lun_blk_devs[lun];

  status = ufs_read_lun_capacity(dev, lun);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to read LUN:%d capacity, status=%d\n", lun, status);
    return status;
  }

  rd_capa16_buf = (ufs_readcapa16_data_t*)hba->req_buf;
  UFS_DBG("UFS device LUN:%d log_blk_addr:0x%lx log_blk_len=0x%x\n", lun,
          htobe64(rd_capa16_buf->log_blk_addr), htobe32(rd_capa16_buf->blk_len));

  dbg_dump_buffer((uint8_t*)rd_capa16_buf, UFS_READ_CAPA16_LEN, "read_capacity16");
  lun_blk_dev->block_info.block_count = htobe64(rd_capa16_buf->log_blk_addr);
  lun_blk_dev->block_info.block_size = htobe32(rd_capa16_buf->blk_len);
  lun_blk_dev->block_info.max_transfer_size = lun_blk_dev->block_info.block_size;
  lun_blk_dev->block_info.flags = 0;

  snprintf(disk_name, sizeof(disk_name), "ufs-disk-%d", lun);
  device_add_args_t block_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = disk_name,
      .ctx = lun_blk_dev,
      .ops = &ufs_lun_dev_proto,
      .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
      .flags = DEVICE_ADD_INVISIBLE,
      .proto_ops = &ufs_lun_blk_ops,
  };

  status = device_add(dev->zxdev, &block_args, &lun_blk_dev->zxdev);
  if (status != ZX_OK) {
    UFS_ERROR("Failed to create ufs_disk for LUN=%d, status=%d\n", lun, status);
    return status;
  }

  // Set lun_id to indicate that it is active now.
  lun_blk_dev->lun_id = lun;

  return ZX_OK;
}

uint8_t ufs_activate_luns(ufshc_dev_t* dev) {
  ufs_lun_blk_dev_t* lun_blk_dev;
  zx_status_t status;
  uint8_t num_lun_active = 0;

  for (uint32_t lun = 0; lun < UFS_MAX_WLUN; lun++) {
    lun_blk_dev = &dev->lun_blk_devs[lun];
    // Set lun_id to LUN_INACTIVE to begin
    lun_blk_dev->lun_id = INACTIVE_LUN;
    status = ufs_send_inquiry(dev, lun);
    if (status != ZX_OK) {
      UFS_ERROR("Failed to inquire LUN:%d, status=%d\n", lun, status);
      continue;
    }

    status = ufs_check_lun_ready(dev, lun);
    if (status != ZX_OK) {
      UFS_ERROR("LUN:%d not ready!, status=%d\n", lun, status);
      continue;
    }

    status = ufs_add_lun_blk_dev(dev, lun);
    if (status != ZX_OK)
      continue;

    num_lun_active++;
  }

  return num_lun_active;
}

static zx_status_t ufs_worker_thread(void* arg) {
  ufshc_dev_t* dev = (ufshc_dev_t*)arg;
  ufs_lun_blk_dev_t* lun_blk_dev;
  uint8_t num_luns_active;

  num_luns_active = ufs_activate_luns(dev);
  if (!num_luns_active) {
    UFS_ERROR("Failed to activate LUN!\n");
    return ZX_ERR_BAD_STATE;
  }

  for (uint8_t lun = 0; lun < UFS_MAX_WLUN; lun++) {
    lun_blk_dev = &dev->lun_blk_devs[lun];
    if (lun_blk_dev->lun_id != INACTIVE_LUN)
      device_make_visible(lun_blk_dev->zxdev, NULL);
  }

  return ZX_OK;
}

zx_status_t ufs_create_worker_thread(ufshc_dev_t* dev) {
  return (thrd_create_with_name(&dev->worker_thread, ufs_worker_thread, dev, "ufs_worker_thread"));
}

zx_status_t ufshc_init(ufshc_dev_t* dev, ufs_hba_variant_ops_t* ufs_hi3660_vops) {
  ufs_hba_t* hba;
  zx_status_t status;

  hba = &dev->ufs_hba;
  ufshc_config_init(hba);
  hba->vops = ufs_hi3660_vops;

  status = ufshc_enable(dev);
  if (status != ZX_OK) {
    UFS_ERROR("UFS HC enabling failed!:%d\n", status);
    return status;
  }
  UFS_DBG("UFS HC enable Success.\n");

  status = ufshc_host_init(dev);
  if (status != ZX_OK)
    goto fail;

  status = ufshc_device_init(dev);
  if (status != ZX_OK)
    goto fail;

  status = ufshc_get_device_info(dev);
  if (status != ZX_OK)
    goto fail;

  return ZX_OK;

fail:
  ufshc_release(dev);
  return status;
}
