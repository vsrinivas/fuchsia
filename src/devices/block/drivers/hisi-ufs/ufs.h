// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_HISI_UFS_UFS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_HISI_UFS_UFS_H_

#include <threads.h>
#include <zircon/compiler.h>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/platform/bus.h>

#define UFS_BIT(x) (1L << (x))
#define LOWER_32_BITS(x) ((uint32_t)((x)&0xFFFFFFFFUL))
#define UPPER_32_BITS(x) ((uint32_t)((x) >> 32))
#define clr_bits(v, a) writel(readl(a) & (uint32_t) ~(v), (a))

#define UFS_ERROR(fmt, ...) zxlogf(ERROR, "[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define UFS_WARN(fmt, ...) zxlogf(WARN, "[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define UFS_INFO(fmt, ...) zxlogf(INFO, "[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
// Uncomment below line for more logs
// #define UFS_DEBUG
#ifdef UFS_DEBUG
#define UFS_DBG(fmt, ...) zxlogf(INFO, "[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define UFS_DBG(fmt, ...) /*nothing*/
#endif

// HCE - Host Controller Enable 34h
#define CONTROLLER_ENABLE UFS_BIT(0)
#define CONTROLLER_DISABLE 0x0

#define UFS_HCS_DP_BIT UFS_BIT(3)
#define UFS_HCS_UCRDY UFS_BIT(3)

// HCE Interrupt status control
#define UFS_IS_UE_BIT UFS_BIT(2)
#define UFS_IS_ULSS_BIT UFS_BIT(8)
#define UFS_IS_UCCS_BIT UFS_BIT(10)
#define UFS_UTP_RUN_BIT UFS_BIT(0)

#define UIC_LINK_STARTUP_CMD 0x16
#define UFS_HCLKDIV_NORMAL_VAL 0xE4
#define UFS_AHT_AH8ITV_MASK 0x3FF
#define UFS_AHT_AH8_TIMER 0x1001

#define UFS_SCTRL_CLK_GATE_BYPASS_MASK 0x3F
#define UFS_SCTRL_SYSCTRL_BYPASS_MASK (0x3F << 16)
#define UFS_SCTRL_CLK_GATE_BYPASS 0x18
#define UFS_SCTRL_SYSCTRL 0x5C

// UFS Command codes
#define READ_DESC_OPCODE 0x01
#define WRITE_DESC_OPCODE 0x02
#define READ_FLAG_OPCODE 0x05
#define SET_FLAG_OPCODE 0x06

// UFS SCSI command codes
#define TEST_UNIT_OPCODE 0x00
#define INQUIRY_OPCODE 0x12
#define READ_CAPA16_OPCODE 0x9E
#define READ10_OPCODE 0x28

#define FLAG_ID_FDEVICE_INIT 0x01

// Descriptor Idns
#define STANDARD_RD_REQ 0x01
#define STANDARD_WR_REQ 0x81
#define DEVICE_DESC_IDN 0x00
#define DEVICE_DESC_LEN 0x40
#define UPIU_CDB_MAX_LEN 16
#define UFS_MAX_WLUN 0x04

#define ALIGNED_UPIU_SIZE 512
#define PRDT_BUF_SIZE 0x40000
#define DATA_REQ_SIZE 4096
#define UFS_INQUIRY_TFR_LEN 36
#define UFS_INQUIRY_VENDOR_OFF 8
#define UFS_INQUIRY_MODEL_OFF 16
#define UFS_READ_CAPA16_LEN 32
#define UFS_READ_CAPA16_SACT 0x10
#define UFS_DEV_SECT_SIZE 0x1000

// UFSHC UPRO configurations
#define UPRO_MPHY_CTRL 0xD0C10000
#define UPRO_MPHY_FSM_TX0 0x00410000
#define UPRO_MPHY_FSM_TX1 0x00410001
#define UPRO_PA_TX_LCC_CTRL 0x155E0000
#define UPRO_MK2_EXTN_SUP 0xD0AB0000
#define UPRO_ERR_PA_IND 0xD0610000

#define MPHY_ATTR_DEMPH_ADDR1 0x1002
#define MPHY_ATTR_DEMPH_ADDR2 0x1102
#define MPHY_ATTR_DEMPH_ADDR3 0x1003
#define MPHY_ATTR_DEMPH_ADDR4 0x1103
#define MPHY_ATTR_DEMPH_VAL1 0xAC78
#define MPHY_ATTR_DEMPH_VAL2 0x2440

#define MPHY_ATTR_DEMPH_ADDR_MSB 0x81170000
#define MPHY_ATTR_DEMPH_ADDR_LSB 0x81160000
#define MPHY_ATTR_DEMPH_VAL_MSB 0x81190000
#define MPHY_ATTR_DEMPH_VAL_LSB 0x81180000
#define MPHY_ATTR_DEMPH_CTRL 0x811C0000

#define BAD_SLOT 0x55
#define NOP_RETRY_COUNT 20
#define MPHY_TX_FSM_RETRY_COUNT 500
#define LINK_STARTUP_UCCS_RETRY_COUNT 200

#define UFS_NUTMRS_SHIFT 16
#define UTP_UFS_STORAGE_CMD (1 << 4)

#define UFS_UPIU_REQ_HDR_LEN 12
#define UFS_RESP_LEN_OFF_H 6
#define UFS_RESP_LEN_OFF_L 7

// UFS device descriptor offsets
#define UFS_DEV_DESC_NUM_LUNS 0x06
#define UFS_DEV_DESC_MANF_ID_H 0x18
#define UFS_DEV_DESC_MANF_ID_L 0x19
#define UFS_READ_DESC_MIN_LEN 0x02

#define SCSI_CMD_STATUS_GOOD 0x0
#define SCSI_CMD_STATUS_CHK_COND 0x02

// UFS HC Register Offsets
enum {
  REG_CONTROLLER_CAPABILITIES = 0x00,
  REG_UFS_VERSION = 0x08,
  REG_CONTROLLER_DEV_ID = 0x10,
  REG_CONTROLLER_PROD_ID = 0x14,
  REG_CONTROLLER_AHIT = 0x18,
  REG_INTERRUPT_STATUS = 0x20,
  REG_INTERRUPT_ENABLE = 0x24,
  REG_CONTROLLER_STATUS = 0x30,
  REG_CONTROLLER_ENABLE = 0x34,
  REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER = 0x38,
  REG_UIC_ERROR_CODE_DATA_LINK_LAYER = 0x3C,
  REG_UIC_ERROR_CODE_NETWORK_LAYER = 0x40,
  REG_UIC_ERROR_CODE_TRANSPORT_LAYER = 0x44,
  REG_UIC_ERROR_CODE_DME = 0x48,
  REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL = 0x4C,
  REG_UTP_TRANSFER_REQ_LIST_BASE_L = 0x50,
  REG_UTP_TRANSFER_REQ_LIST_BASE_H = 0x54,
  REG_UTP_TRANSFER_REQ_DOOR_BELL = 0x58,
  REG_UTP_TRANSFER_REQ_LIST_CLEAR = 0x5C,
  REG_UTP_TRANSFER_REQ_LIST_RUN_STOP = 0x60,
  REG_UTP_TASK_REQ_LIST_BASE_L = 0x70,
  REG_UTP_TASK_REQ_LIST_BASE_H = 0x74,
  REG_UTP_TASK_REQ_DOOR_BELL = 0x78,
  REG_UTP_TASK_REQ_LIST_CLEAR = 0x7C,
  REG_UTP_TASK_REQ_LIST_RUN_STOP = 0x80,
  REG_UIC_COMMAND = 0x90,
  REG_UIC_COMMAND_ARG_1 = 0x94,
  REG_UIC_COMMAND_ARG_2 = 0x98,
  REG_UIC_COMMAND_ARG_3 = 0x9C,
  REG_UFS_HCLKDIV_OFF = 0xFC,
};

// UFS status / Error codes used as return values
enum {
  UFS_LINK_STARTUP_FAIL = -0x01,
  UFS_UTRD_DOORBELL_TIMEOUT = -0x02,
  UFS_NOP_RESP_FAIL = -0x03,
  UFS_NOP_OUT_OCS_FAIL = -0x04,
  UFS_INVALID_NOP_IN = -0x05,
  // UPIU response error codes
  UPIU_RESP_COND_FAIL = -0x06,
  UPIU_RESP_STAT_FAIL = -0x07,
};

enum {
  MMIO_UFSHC,
  MMIO_UFS_SCTRL,
};

// Rate
enum {
  UFS_RATE_A = 1,
  UFS_RATE_B,
};

// Controller capability masks
enum {
  MASK_TRANSFER_REQUESTS_SLOTS = 0x0000001F,
  MASK_TASK_MANAGEMENT_REQUEST_SLOTS = 0x00070000,
};

enum uic_dme_type {
  // Configuration
  DME_GET = 0x01,
  DME_SET = 0x02,
  // Control
  DME_ENABLE = 0x12,
};

enum utp_data_tfr_dirn {
  UTP_NO_DATA_TFR,
  UTP_HOST_TO_DEVICE = 0x02,
  UTP_DEVICE_TO_HOST = 0x04,
};

enum upiu_cmd_flags {
  UPIU_CMD_FLAGS_NONE = 0x00,
  UPIU_CMD_FLAGS_WRITE = 0x20,
  UPIU_CMD_FLAGS_READ = 0x40,
  UPIU_CMD_FLAGS_MAX,
};

// UFS UPIU transaction type
enum upiu_trans_type {
  UPIU_TYPE_NOP_OUT = 0x00,
  UPIU_TYPE_CMD = 0x01,
  UPIU_TYPE_QUERY_REQ = 0x16,
  UPIU_TYPE_NOP_IN = 0x20,
  UPIU_TYPE_REJECT = 0x3F,
};

enum dma_direction {
  UFS_DMA_TO_DEVICE = 0x01,
  UFS_DMA_FROM_DEVICE = 0x02,
  UFS_DMA_NONE = 0x03,
};

enum ufs_link_change_stage {
  PRE_CHANGE,
  POST_CHANGE,
};

typedef struct {
  uint64_t log_blk_addr;
  uint32_t blk_len;
  uint8_t prot_info;
  uint8_t log_blk_per_phys_blk_exp;
  uint16_t low_align_log_blk_addr;
  uint8_t res[16];
} ufs_readcapa16_data_t;

// UFSHCI PRD Entry
typedef struct {
  uint32_t base_addr;
  uint32_t upper_addr;
  uint32_t res1;
  uint32_t size;
} ufshcd_prd_t;

// NOP OUT UPIU
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t res1;
  uint8_t task_tag;
  uint32_t res2;
  uint8_t tot_ehs_len;
  uint8_t res3;
  uint16_t data_seg_len;
  uint8_t res4[20];
} ufs_nop_req_upiu_t;

// NOP IN UPIU
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t res1;
  uint8_t task_tag;
  uint8_t res2_0;
  uint8_t res2_1;
  uint8_t resp;
  uint8_t res3;
  uint8_t tot_ehs_len;
  uint8_t device_info;
  uint16_t data_seg_len;
  uint8_t res4[20];
} ufs_nop_resp_upiu_t;

// Query UPIU
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t res1;
  uint8_t task_tag;
  uint8_t res2;
  uint8_t query_func;
  uint8_t query_resp;
  uint8_t res3;
  uint8_t tot_ehs_len;
  uint8_t res4;
  uint16_t data_seg_len;
  uint8_t tsf[16];
  uint32_t res5;
} ufs_query_req_upiu_t;

// UFS Command Descriptor structure
typedef struct {
  uint8_t cmd_upiu[ALIGNED_UPIU_SIZE];
  uint8_t resp_upiu[ALIGNED_UPIU_SIZE];
  ufshcd_prd_t prd_table[128];
} utp_tfr_cmd_desc_t;

// Command UPIU structure
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t lun;
  uint8_t task_tag;
  uint8_t cmd_set_type;
  uint8_t res1_0;
  uint8_t res1_1;
  uint8_t res1_2;
  uint8_t tot_ehs_len;
  uint8_t res2;
  uint16_t data_seg_len;
  uint32_t exp_data_xfer_len;
  uint8_t cdb[UPIU_CDB_MAX_LEN];
} ufs_utp_cmd_upiu_t;

// Response UPIU structure
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t lun;
  uint8_t task_tag;
  uint8_t cmd_set_type;
  uint8_t res1;
  uint8_t resp;
  uint8_t status;
  uint8_t tot_ehs_len;
  uint8_t device_info;
  uint16_t data_seg_len;
  uint32_t resd_xfer_count;
  uint32_t res2;
  uint32_t res3;
  uint32_t res4;
  uint32_t res5;
  uint16_t sense_data_len;
  uint8_t sense_data[18];
} ufs_utp_resp_upiu_t;

// UTRD structure
typedef struct {
  uint8_t crypt_cci;
  uint8_t res1_0;
  uint8_t crypt_en;
  uint8_t ct_flags;
  uint32_t dunl;
  uint8_t ocs;
  uint8_t res3_0;
  uint8_t res3_1;
  uint8_t res3_2;
  uint32_t dunu;
  uint32_t ucdba;
  uint32_t ucdbau;
  uint16_t resp_upiu_len;
  uint16_t resp_upiu_off;
  uint16_t prd_table_len;
  uint16_t prd_table_off;
} utp_tfr_req_desc_t;

// Task Manage request
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t lun;
  uint8_t res1;
  uint8_t tm_fn;
  uint8_t res2_0;
  uint8_t res2_1;
  uint8_t tot_ehs_len;
  uint8_t res3;
  uint16_t data_seg_len;
  uint32_t ip_param_1;
  uint32_t ip_param_2;
  uint32_t ip_param_3;
  uint32_t res4;
  uint32_t res5;
} ufs_tm_req_upiu_t;

// Task Manage response
typedef struct {
  uint8_t trans_type;
  uint8_t flags;
  uint8_t lun;
  uint8_t task_tag;
  uint8_t res1_0;
  uint8_t res1_1;
  uint8_t resp;
  uint8_t res2;
  uint8_t tot_ehs_len;
  uint8_t res3;
  uint16_t data_seg_len;
  uint32_t ip_param_1;
  uint32_t ip_param_2;
  uint32_t res4;
  uint32_t res5;
  uint32_t res6;
} ufs_tm_resp_upiu_t;

// UTMRD structure
typedef struct {
  uint8_t res1_0;
  uint8_t res1_1;
  uint8_t res1_2;
  uint8_t intr_flag;
  uint32_t res2;
  uint8_t ocs;
  uint8_t res3_0;
  uint8_t res3_1;
  uint8_t res3_2;
  uint32_t res4;
  ufs_tm_req_upiu_t tm_req_upiu;
  ufs_tm_resp_upiu_t tm_resp_upiu;
} utp_task_req_desc_t;

// Local reference block
typedef struct {
  uint8_t cmd_type;
  uint8_t data_direction;
  uint8_t rw_flags;
  uint8_t ocs;
  uint8_t xfer_cmd_status;
  uint32_t tfr_size;
  uint8_t task_tag;
  uint32_t lun;
  utp_tfr_req_desc_t* utrd;
  ufs_utp_cmd_upiu_t* cmd_upiu;
  ufs_utp_resp_upiu_t* resp_upiu;
  ufshcd_prd_t* prdt;
} ufs_hcd_lrb_t;

typedef struct {
  const char* name;
  zx_status_t (*link_startup)(volatile void* regs, uint8_t status);
} ufs_hba_variant_ops_t;

// UFS Host bus adaptor
typedef struct ufs_hba {
  uint8_t nutmrs;
  uint8_t nutrs;
  uint32_t caps;
  uint32_t ufs_version;
  uint8_t num_lun;
  uint8_t active_lun;
  uint16_t manufacturer_id;

  // UFS Command descriptor
  io_buffer_t ucdl_dma_buf;
  // UTP Transfer request descriptor
  io_buffer_t utrl_dma_buf;
  // UTP Task management descriptor
  io_buffer_t utmrl_dma_buf;
  // UFS request buffer
  io_buffer_t req_dma_buf;
  utp_tfr_cmd_desc_t* cmd_desc;
  utp_tfr_req_desc_t* tfr_desc;
  utp_task_req_desc_t* req_desc;
  ufs_hcd_lrb_t* lrb_buf;
  void* req_buf;
  ulong outstanding_xfer_reqs;
  ulong outstanding_tm_tasks;
  zx_time_t timeout;
  ufs_hba_variant_ops_t* vops;
} ufs_hba_t;

// UFS LUN Block device
typedef struct ufs_lun_blk_dev {
  zx_device_t* zxdev;
  block_info_t block_info;
  int lun_id;
} ufs_lun_blk_dev_t;

// UFS device
typedef struct ufshc_dev {
  pdev_protocol_t pdev;
  zx_device_t* zxdev;
  ufs_lun_blk_dev_t lun_blk_devs[UFS_MAX_WLUN];
  mmio_buffer_t ufshc_mmio;
  zx_handle_t bti;
  ufs_hba_t ufs_hba;
  thrd_t worker_thread;
} ufshc_dev_t;

static inline int32_t find_first_zero_bit(ulong* addr, uint8_t bits) {
  ulong* value = addr;
  int32_t i;

  for (i = 0; i < bits; i++) {
    if (0 == (*value & (1 << i)))
      return i;
  }

  return -1;
}

#ifdef UFS_DEBUG
static inline void dbg_dump_buffer(uint8_t* buf, uint32_t len, const char* name) {
  zxlogf(INFO, "%s_buffer:", name);
  for (uint32_t index = 0; index < len; index++) {
    zxlogf(INFO, "buf[%d]=0x%x ", index, buf[index]);
    if (index && !(index % 10))
      zxlogf(INFO, "");
  }
  zxlogf(INFO, "");
}
#else
static inline void dbg_dump_buffer(uint8_t* buf, uint32_t len, const char* name) {}
#endif

zx_status_t ufshc_send_uic_command(volatile void* regs, uint32_t command, uint32_t arg1,
                                   uint32_t arg3);
uint32_t ufshc_uic_cmd_read(volatile void* regs, uint32_t command, uint32_t arg1);
void ufshc_disable_auto_h8(volatile void* regs);
void ufshc_check_h8(volatile void* regs);
zx_status_t ufshc_init(ufshc_dev_t* dev, ufs_hba_variant_ops_t* ufs_hi3660_vops);
zx_status_t ufs_create_worker_thread(ufshc_dev_t* dev);

#endif  // SRC_STORAGE_BLOCK_DRIVERS_HISI_UFS_UFS_H_
