// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <syslog/wire_format.h>
#include <zircon/syscalls/log.h>

#include "gtest/gtest.h"

namespace {

// This function will fail to build when zircon ABI changes
// and we will need to manually roll changes.
TEST(CAbi, Abi) {
  static_assert(FX_LOG_MAX_DATAGRAM_LEN == 2032, "");
  static_assert(sizeof(fx_log_metadata_t) == 32, "");
  fx_log_packet_t packet;
  static_assert(sizeof(packet.data) == 2000, "");

  // test alignment
  static_assert(offsetof(fx_log_packet_t, data) == 32, "");
  static_assert(offsetof(fx_log_packet_t, metadata) == 0, "");
  static_assert(offsetof(fx_log_metadata_t, pid) == 0, "");
  static_assert(offsetof(fx_log_metadata_t, tid) == 8, "");
  static_assert(offsetof(fx_log_metadata_t, time) == 16, "");
  static_assert(offsetof(fx_log_metadata_t, severity) == 24, "");
  static_assert(offsetof(fx_log_metadata_t, dropped_logs) == 28, "");
}

// This function will fail to build when zircon ABI changes
// and we will need to manually roll changes.
TEST(CAbi, LogRecordAbi) {
  static_assert(ZX_LOG_RECORD_MAX == 256, "");
  static_assert(ZX_LOG_FLAG_READABLE == 0x40000000, "");

  // test alignment
  static_assert(offsetof(zx_log_record_t, timestamp) == 8, "");
  static_assert(offsetof(zx_log_record_t, pid) == 16, "");
  static_assert(offsetof(zx_log_record_t, tid) == 24, "");
  static_assert(offsetof(zx_log_record_t, data) == 32, "");
}

}  // namespace
