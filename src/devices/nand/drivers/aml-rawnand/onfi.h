// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_ONFI_H_
#define SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_ONFI_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

static constexpr uint32_t NAND_CE0 = (0xe << 10);
static constexpr uint32_t NAND_CE1 = (0xd << 10);

static constexpr uint32_t NAND_NCE = 0x01;
static constexpr uint32_t NAND_CLE = 0x02;
static constexpr uint32_t NAND_ALE = 0x04;

static constexpr uint32_t NAND_CTRL_CLE = (NAND_NCE | NAND_CLE);
static constexpr uint32_t NAND_CTRL_ALE = (NAND_NCE | NAND_ALE);
static constexpr uint32_t NAND_CTRL_CHANGE = 0x80;

static constexpr uint32_t NAND_CMD_READ0 = 0;
static constexpr uint32_t NAND_CMD_READ1 = 1;
static constexpr uint32_t NAND_CMD_PAGEPROG = 0x10;
static constexpr uint32_t NAND_CMD_READOOB = 0x50;
static constexpr uint32_t NAND_CMD_ERASE1 = 0x60;
static constexpr uint32_t NAND_CMD_STATUS = 0x70;
static constexpr uint32_t NAND_CMD_SEQIN = 0x80;
static constexpr uint32_t NAND_CMD_READID = 0x90;
static constexpr uint32_t NAND_CMD_ERASE2 = 0xd0;
static constexpr uint32_t NAND_CMD_RESET = 0xff;
static constexpr int32_t NAND_CMD_NONE = -1;

// Extended commands for large page devices.
static constexpr uint32_t NAND_CMD_READSTART = 0x30;

// Status.
static constexpr uint32_t NAND_STATUS_FAIL = 0x01;
static constexpr uint32_t NAND_STATUS_FAIL_N1 = 0x02;
static constexpr uint32_t NAND_STATUS_TRUE_READY = 0x20;
static constexpr uint32_t NAND_STATUS_READY = 0x40;
static constexpr uint32_t NAND_STATUS_WP = 0x80;

struct nand_timings {
  uint32_t tRC_min;
  uint32_t tREA_max;
  uint32_t RHOH_min;
};

struct polling_timing_t {
  zx::duration min;
  zx::duration interval;
};

struct polling_timings_t {
  polling_timing_t cmd_flush;
  polling_timing_t write;
  polling_timing_t erase;
};

struct nand_chip_table {
  uint8_t manufacturer_id;
  uint8_t device_id;
  const char* manufacturer_name;
  const char* device_name;
  struct nand_timings timings;
  polling_timings_t polling_timings;
  uint32_t chip_delay_us;  // Delay us after enqueuing command.
  // extended_id_nand -> pagesize, erase blocksize, OOB size
  // could vary given the same device id.
  bool extended_id_nand;
  uint64_t chipsize;  // MiB.
  // Valid only if extended_id_nand is false.
  uint32_t page_size;         // Bytes.
  uint32_t oobsize;           // Bytes.
  uint32_t erase_block_size;  // Bytes.
  uint32_t bus_width;         // 8 vs 16 bit.
};

class Onfi {
 public:
  virtual ~Onfi() = default;

  // OnfiWait() and OnfiCommand() are generic ONFI protocol compliant.
  // Sends onfi command down to the controller.
  virtual void OnfiCommand(uint32_t command, int32_t column, int32_t page_addr,
                           uint32_t capacity_mb, uint32_t chip_delay_us, int buswidth_16);

  // Generic wait function used by both program (write) and erase functionality.
  virtual zx_status_t OnfiWait(zx::duration timeout, zx::duration first_interval,
                               zx::duration polling_interval);

  // Sets the device-specific functions to send a command and read a byte.
  void Init(fit::function<void(int32_t cmd, uint32_t ctrl)> cmd_ctrl,
            fit::function<uint8_t()> read_byte);

  // Finds the entry in the NAND chip table database based on manufacturer
  // id and device id.
  struct nand_chip_table* FindNandChipTable(uint8_t manuf_id, uint8_t device_id);

 private:
  fit::function<void(int32_t cmd, uint32_t ctrl)> cmd_ctrl_;
  fit::function<uint8_t()> read_byte_;
};

#endif  // SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_ONFI_H_
