// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/zx/vmo.h>
#include <zircon/limits.h>

#include <cinttypes>
#include <vector>

#include <tee-client-api/tee-client-types.h>

class SecmemSession {
 public:
  static fpromise::result<SecmemSession, fuchsia::tee::ApplicationSyncPtr> TryOpen(
      fuchsia::tee::ApplicationSyncPtr tee_connection);

  SecmemSession(SecmemSession&&) = default;
  SecmemSession& operator=(SecmemSession&&) = default;

  ~SecmemSession();

  [[nodiscard]] bool DetectIsAdjustAndSkipDeviceSecureModeUpdateAvailable();
  // If !DetectIsAdjustAndSkipDeviceSecureModeUpdateAvailable(), is_skip_device_secure_mode_update
  // must be false.
  [[nodiscard]] TEEC_Result ProtectMemoryRange(uint32_t start, uint32_t length,
                                               bool is_enable_protection);
  [[nodiscard]] TEEC_Result AdjustMemoryRange(uint32_t start, uint32_t length,
                                              uint32_t adjustment_magnitude, bool at_start,
                                              bool longer);
  [[nodiscard]] TEEC_Result ZeroSubRange(bool is_covering_range_explicit, uint32_t start,
                                         uint32_t length);
  [[nodiscard]] TEEC_Result AllocateSecureMemory(uint32_t* start, uint32_t* length);
  void DumpRanges();

 private:
  static constexpr uint64_t kParameterAlignment = 32u;
  static constexpr uint64_t kParameterBufferSize = ZX_PAGE_SIZE;

  static void PackUint32Parameter(uint32_t value, std::vector<uint8_t>* buffer);
  static fpromise::result<uint32_t> UnpackUint32Parameter(const std::vector<uint8_t>& buffer,
                                                          size_t* offset_in_out);

  explicit SecmemSession(uint32_t session_id, fuchsia::tee::ApplicationSyncPtr tee_connection)
      : session_id_(session_id), tee_connection_(std::move(tee_connection)) {}

  void OpenSession();
  [[nodiscard]] TEEC_Result InvokeSecmemCommand(uint32_t command,
                                                std::vector<uint8_t>* cmd_buffer_vec);
  [[nodiscard]] TEEC_Result InvokeProtectMemory(uint32_t start, uint32_t length,
                                                uint32_t enable_flags);

  uint32_t session_id_;
  fuchsia::tee::ApplicationSyncPtr tee_connection_;

  bool is_detect_called_ = false;
  bool is_adjust_known_available_ = false;
};

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_
