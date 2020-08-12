// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_RPMB_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_RPMB_H_

namespace optee {

// Structure of RPMB data frame.
struct RpmbFrame {
  static constexpr size_t kRpmbSizeStuff = 196;
  static constexpr size_t kRpmbSizeMac = 32;
  static constexpr size_t kRpmbSizeData = 256;
  static constexpr size_t kRpmbSizeNonce = 16;

  static constexpr int kRpmbRequestKey = 1;
  static constexpr int kRpmbRequestWCounter = 2;
  static constexpr int kRpmbRequestWriteData = 3;
  static constexpr int kRpmbRequestReadData = 4;
  static constexpr int kRpmbRequestStatus = 5;

  uint8_t stuff[kRpmbSizeStuff];
  uint8_t mac[kRpmbSizeMac];
  uint8_t data[kRpmbSizeData];
  uint8_t nonce[kRpmbSizeNonce];
  uint32_t write_counter;
  uint16_t address;
  uint16_t block_count;
  uint16_t result;
  uint16_t request;
} __PACKED;

// RPMB request from TEE to REE
struct RpmbReq {
  static constexpr int kCmdDataRequest = 0;
  static constexpr int kCmdGetDevInfo = 1;

  uint16_t cmd;
  uint16_t dev_id;
  uint16_t block_count;
  RpmbFrame frames[];
} __PACKED;

// Response to device info request
struct RpmbDevInfo {
  static constexpr size_t kRpmbCidSize = 16;

  static constexpr int kRpmbCmdRetOK = 0;
  static constexpr int kRpmbCmdRetError = 1;

  uint8_t cid[kRpmbCidSize];
  uint8_t rpmb_size;               // EXT CSD-slice 168: RPMB Size
  uint8_t rel_write_sector_count;  // EXT CSD-slice 222: Reliable Write Sector
  uint8_t ret_code;
} __PACKED;

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_RPMB_H_
