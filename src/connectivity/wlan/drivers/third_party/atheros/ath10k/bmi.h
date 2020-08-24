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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_ATHEROS_ATH10K_BMI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_ATHEROS_ATH10K_BMI_H_

#include "core.h"

/*
 * Bootloader Messaging Interface (BMI)
 *
 * BMI is a very simple messaging interface used during initialization
 * to read memory, write memory, execute code, and to define an
 * application entry PC.
 *
 * It is used to download an application to QCA988x, to provide
 * patches to code that is already resident on QCA988x, and generally
 * to examine and modify state.  The Host has an opportunity to use
 * BMI only once during bootup.  Once the Host issues a BMI_DONE
 * command, this opportunity ends.
 *
 * The Host writes BMI requests to mailbox0, and reads BMI responses
 * from mailbox0.   BMI requests all begin with a command
 * (see below for specific commands), and are followed by
 * command-specific data.
 *
 * Flow control:
 * The Host can only issue a command once the Target gives it a
 * "BMI Command Credit", using AR8K Counter #4.  As soon as the
 * Target has completed a command, it issues another BMI Command
 * Credit (so the Host can issue the next command).
 *
 * BMI handles all required Target-side cache flushing.
 */

/* Maximum data size used for BMI transfers */
#define BMI_MAX_DATA_SIZE 256

/* len = cmd + addr + length */
#define BMI_MAX_CMDBUF_SIZE \
  (BMI_MAX_DATA_SIZE + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t))

/* BMI Commands */

enum bmi_cmd_id {
  // clang-format off
    BMI_NO_COMMAND          = 0,
    BMI_DONE                = 1,
    BMI_READ_MEMORY         = 2,
    BMI_WRITE_MEMORY        = 3,
    BMI_EXECUTE             = 4,
    BMI_SET_APP_START       = 5,
    BMI_READ_SOC_REGISTER   = 6,
    BMI_READ_SOC_WORD       = 6,
    BMI_WRITE_SOC_REGISTER  = 7,
    BMI_WRITE_SOC_WORD      = 7,
    BMI_GET_TARGET_ID       = 8,
    BMI_GET_TARGET_INFO     = 8,
    BMI_ROMPATCH_INSTALL    = 9,
    BMI_ROMPATCH_UNINSTALL  = 10,
    BMI_ROMPATCH_ACTIVATE   = 11,
    BMI_ROMPATCH_DEACTIVATE = 12,
    BMI_LZ_STREAM_START     = 13, /* should be followed by LZ_DATA */
    BMI_LZ_DATA             = 14,
    BMI_NVRAM_PROCESS       = 15,
  // clang-format on
};

// clang-format off
#define BMI_NVRAM_SEG_NAME_SZ         16

#define BMI_PARAM_GET_EEPROM_BOARD_ID 0x10
#define BMI_PARAM_GET_FLASH_BOARD_ID  0x8000
#define BMI_PARAM_FLASH_SECTION_ALL   0x10000

#define ATH10K_BMI_BOARD_ID_FROM_OTP_MASK   0x7c00
#define ATH10K_BMI_BOARD_ID_FROM_OTP_LSB    10

#define ATH10K_BMI_CHIP_ID_FROM_OTP_MASK    0x18000
#define ATH10K_BMI_CHIP_ID_FROM_OTP_LSB     15

#define ATH10K_BMI_BOARD_ID_STATUS_MASK     0xff
// clang-format on

struct bmi_cmd {
  uint32_t id; /* enum bmi_cmd_id */
  union {
    struct {
    } done;
    struct {
      uint32_t addr;
      uint32_t len;
    } read_mem;
    struct {
      uint32_t addr;
      uint32_t len;
      uint8_t payload[0];
    } write_mem;
    struct {
      uint32_t addr;
      uint32_t param;
    } execute;
    struct {
      uint32_t addr;
    } set_app_start;
    struct {
      uint32_t addr;
    } read_soc_reg;
    struct {
      uint32_t addr;
      uint32_t value;
    } write_soc_reg;
    struct {
    } get_target_info;
    struct {
      uint32_t rom_addr;
      uint32_t ram_addr; /* or value */
      uint32_t size;
      uint32_t activate; /* 0=install, but dont activate */
    } rompatch_install;
    struct {
      uint32_t patch_id;
    } rompatch_uninstall;
    struct {
      uint32_t count;
      uint32_t patch_ids[0]; /* length of @count */
    } rompatch_activate;
    struct {
      uint32_t count;
      uint32_t patch_ids[0]; /* length of @count */
    } rompatch_deactivate;
    struct {
      uint32_t addr;
    } lz_start;
    struct {
      uint32_t len;       /* max BMI_MAX_DATA_SIZE */
      uint8_t payload[0]; /* length of @len */
    } lz_data;
    struct {
      uint8_t name[BMI_NVRAM_SEG_NAME_SZ];
    } nvram_process;
    uint8_t payload[BMI_MAX_CMDBUF_SIZE];
  };
} __PACKED;

union bmi_resp {
  struct {
    uint8_t payload[0];
  } read_mem;
  struct {
    uint32_t result;
  } execute;
  struct {
    uint32_t value;
  } read_soc_reg;
  struct {
    uint32_t len;
    uint32_t version;
    uint32_t type;
  } get_target_info;
  struct {
    uint32_t patch_id;
  } rompatch_install;
  struct {
    uint32_t patch_id;
  } rompatch_uninstall;
  struct {
    /* 0 = nothing executed
     * otherwise = NVRAM segment return value
     */
    uint32_t result;
  } nvram_process;
  uint8_t payload[BMI_MAX_CMDBUF_SIZE];
} __PACKED;

struct bmi_target_info {
  uint32_t version;
  uint32_t type;
};

#define BMI_COMMUNICATION_TIMEOUT ZX_SEC(3)

#define BMI_CE_NUM_TO_TARG 0
#define BMI_CE_NUM_TO_HOST 1

void ath10k_bmi_start(struct ath10k* ar);
zx_status_t ath10k_bmi_done(struct ath10k* ar);
zx_status_t ath10k_bmi_get_target_info(struct ath10k* ar, struct bmi_target_info* target_info);
zx_status_t ath10k_bmi_read_memory(struct ath10k* ar, uint32_t address, void* buffer,
                                   uint32_t length);
zx_status_t ath10k_bmi_write_memory(struct ath10k* ar, uint32_t address, const void* buffer,
                                    uint32_t length);

#define ath10k_bmi_read32(ar, item, val)                       \
  ({                                                           \
    zx_status_t ret;                                           \
    uint32_t addr;                                             \
    uint32_t tmp;                                              \
                                                               \
    addr = host_interest_item_address(HI_ITEM(item));          \
    ret = ath10k_bmi_read_memory(ar, addr, (uint8_t*)&tmp, 4); \
    if (ret == ZX_OK)                                          \
      *val = tmp;                                              \
    ret;                                                       \
  })

#define ath10k_bmi_write32(ar, item, val)                                \
  ({                                                                     \
    zx_status_t ret;                                                     \
    uint32_t address;                                                    \
    uint32_t v = val;                                                    \
                                                                         \
    address = host_interest_item_address(HI_ITEM(item));                 \
    ret = ath10k_bmi_write_memory(ar, address, (uint8_t*)&v, sizeof(v)); \
    ret;                                                                 \
  })

zx_status_t ath10k_bmi_execute(struct ath10k* ar, uint32_t address, uint32_t param,
                               uint32_t* result);
zx_status_t ath10k_bmi_lz_stream_start(struct ath10k* ar, uint32_t address);
zx_status_t ath10k_bmi_lz_data(struct ath10k* ar, const void* buffer, uint32_t length);
zx_status_t ath10k_bmi_fast_download(struct ath10k* ar, uint32_t address, const void* buffer,
                                     uint32_t length);
zx_status_t ath10k_bmi_read_soc_reg(struct ath10k* ar, uint32_t address, uint32_t* reg_val);
zx_status_t ath10k_bmi_write_soc_reg(struct ath10k* ar, uint32_t address, uint32_t reg_val);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_ATHEROS_ATH10K_BMI_H_
