// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_AHCI_SATA_H_
#define SRC_DEVICES_BLOCK_DRIVERS_AHCI_SATA_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/zx/time.h>
#include <zircon/listnode.h>

#include <ddk/device.h>

#include "ahci.h"

#define SATA_CMD_IDENTIFY_DEVICE 0xec
#define SATA_CMD_READ_DMA 0xc8
#define SATA_CMD_READ_DMA_EXT 0x25
#define SATA_CMD_READ_FPDMA_QUEUED 0x60
#define SATA_CMD_WRITE_DMA 0xca
#define SATA_CMD_WRITE_DMA_EXT 0x35
#define SATA_CMD_WRITE_FPDMA_QUEUED 0x61

#define SATA_DEVINFO_SERIAL_LEN 20
#define SATA_DEVINFO_FW_REV_LEN 8
#define SATA_DEVINFO_MODEL_ID_LEN 40

#define SATA_MAX_BLOCK_COUNT 0x10000  // 16-bit count

#define BLOCK_OP(op) ((op)&BLOCK_OP_MASK)

namespace ahci {

class Controller;

// SATA disk identifier. Response to SATA_CMD_IDENTIFY_DEVICE command.
// Generated from ATA Command Set spec (ACS-4).
struct sata_devinfo_response_t {  // 16-bit word offset
  uint16_t general_config;        // 0
  uint16_t _obsolete_1;           // 1
  uint16_t specific_config;       // 2
  uint16_t _obsolete_3;           // 3
  uint16_t _retired_4[2];         // 4-5
  uint16_t _obsolete_6;           // 6
  uint16_t cfa_reserved[2];       // 7-8
  uint16_t _retired_9;            // 9
  union {
    uint16_t word[SATA_DEVINFO_SERIAL_LEN / 2];  // 10-19
    char string[SATA_DEVINFO_SERIAL_LEN];
  } serial;
  uint16_t _retired_20[2];  // 20-21
  uint16_t _obsolete_22;    // 22
  union {
    uint16_t word[SATA_DEVINFO_FW_REV_LEN / 2];  // 23-26
    char string[SATA_DEVINFO_FW_REV_LEN];
  } firmware_rev;
  union {
    uint16_t word[SATA_DEVINFO_MODEL_ID_LEN / 2];  // 27-46
    char string[SATA_DEVINFO_MODEL_ID_LEN];
  } model_id;
  uint16_t _obsolete_47;               // 47
  uint16_t trusted_computing_options;  // 48
  uint16_t capabilities_1;             // 49
  uint16_t capabilities_2;             // 50
  uint16_t _obsolete_51[2];            // 51-52
  uint16_t unnamed_53;                 // 53
  uint16_t _obsolete_54[5];            // 54-58
  uint16_t unnamed_59;                 // 59
  uint32_t lba_capacity;               // 60-61
  uint16_t _obsolete_62;               // 62
  uint16_t unnamed_63;                 // 63
  uint16_t pio_modes;                  // 64
  uint16_t min_dma_cycle_time;         // 65
  uint16_t rec_dma_cycle_time;         // 66
  uint16_t min_pio_cycle_time;         // 67
  uint16_t min_pio_iordy_cycle_time;   // 68
  uint16_t additional_supported;       // 69
  uint16_t _reserved_70;               // 70
  uint16_t _reserved_71[4];            // 71-74
  uint16_t queue_depth;                // 75
  uint16_t sata_capabilities;          // 76
  uint16_t sata_capabilities2;         // 77
  uint16_t sata_features_supported;    // 78
  uint16_t sata_features_enabled;      // 79
  uint16_t major_version;              // 80
  uint16_t minor_version;              // 81
  uint16_t command_set1_0;             // 82
  uint16_t command_set1_1;             // 83
  uint16_t command_set1_2;             // 84 -- see 119
  uint16_t command_set2_0;             // 85
  uint16_t command_set2_1;             // 86
  uint16_t command_set2_2;             // 87 -- see 120
  uint16_t udma_modes;                 // 88
  uint16_t timing_info[2];             // 89-90
  uint16_t apm_level;                  // 91
  uint16_t master_password;            // 92
  uint16_t reset_results;              // 93
  uint16_t _obsolete_94;               // 94
  uint16_t stream_min_size;            // 95
  uint16_t dma_stream_xfer_time;       // 96
  uint16_t stream_access_latency;      // 97
  uint32_t stream_perf_granularity;    // 98-99
  uint64_t lba_capacity2;              // 100-103
  uint16_t pio_stream_xfer_time;       // 104
  uint16_t data_set_max;               // 105
  uint16_t sector_size;                // 106
  uint16_t inter_seek_delay;           // 107
  uint64_t world_wide_name;            // 108-111
  uint16_t _reserved_112[4];           // 112-115
  uint16_t _obsolete_116;              // 116
  uint32_t logical_sector_size;        // 117-118
  uint16_t command_set1_3;             // 119
  uint16_t command_set2_3;             // 120
  uint16_t _reserved_121[6];           // 121-126
  uint16_t _obsolete_127;              // 127
  uint16_t security_status;            // 128
  uint16_t vendor_specific[31];        // 129-159
  uint16_t cfa_reserved2[8];           // 160-167
  uint16_t form_factor;                // 168
  uint16_t data_management_support;    // 169
  uint16_t additional_product_id[4];   // 170-173
  uint16_t _reserved_174[2];           // 174-175
  uint16_t media_serial_number[30];    // 176-205
  uint16_t sct_command_transport;      // 206
  uint16_t reserved_207[2];            // 207-208
  uint16_t sector_alignment;           // 209
  uint32_t wrv_mode3_count;            // 210-211
  uint32_t wrv_mode2_count;            // 212-213
  uint16_t _obsolete_214[3];           // 214-216
  uint16_t rotation_rate;              // 217
  uint16_t _reserved_218;              // 218
  uint16_t _obsolete_219;              // 219
  uint16_t wrv_mode;                   // 220
  uint16_t _reserved_221;              // 221
  uint16_t transport_major_version;    // 222
  uint16_t transport_minor_version;    // 223
  uint16_t _reserved_224[6];           // 224-229
  uint64_t ext_sector_count;           // 230-233
  uint16_t min_microcode_blocks;       // 234
  uint16_t max_microcode_blocks;       // 235
  uint16_t _reserved_236[19];          // 236-254
  uint16_t checksum;                   // 255
} __attribute__((packed));

static_assert(sizeof(sata_devinfo_response_t) == 512);

struct sata_txn_t {
  block_op_t bop;
  list_node_t node;

  zx::time timeout;

  uint8_t cmd;
  uint8_t device;

  zx_status_t status;
  zx_handle_t pmt;
  block_impl_queue_callback completion_cb;
  void* cookie;
};

struct sata_devinfo_t {
  uint32_t block_size;
  uint32_t max_cmd;
};

zx_status_t sata_bind(Controller* controller, zx_device_t* parent, uint32_t port);

static inline void block_complete(sata_txn_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->bop);
}

}  // namespace ahci

#endif  // SRC_DEVICES_BLOCK_DRIVERS_AHCI_SATA_H_
