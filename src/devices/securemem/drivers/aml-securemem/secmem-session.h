// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/vmo.h>
#include <zircon/limits.h>

#include <cinttypes>
#include <vector>

#include <tee-client-api/tee-client-types.h>

class SecmemSession {
 public:
  static fit::result<SecmemSession, fuchsia::tee::DeviceSyncPtr> TryOpen(
      fuchsia::tee::DeviceSyncPtr tee_connection);

  SecmemSession(SecmemSession&&) = default;
  SecmemSession& operator=(SecmemSession&&) = default;

  ~SecmemSession();

  [[nodiscard]] TEEC_Result ProtectMemoryRange(uint32_t start, uint32_t length, bool is_enable);
  [[nodiscard]] TEEC_Result AllocateSecureMemory(uint32_t* start, uint32_t* length);

 private:
  static constexpr uint64_t kParameterAlignment = 32u;
  static constexpr uint64_t kParameterBufferSize = ZX_PAGE_SIZE;

  static void PackUint32Parameter(uint32_t value, std::vector<uint8_t>* buffer);
  static fit::result<uint32_t> UnpackUint32Parameter(const std::vector<uint8_t>& buffer,
                                                     size_t* offset_in_out);

  explicit SecmemSession(uint32_t session_id, fuchsia::tee::DeviceSyncPtr tee_connection)
      : session_id_(session_id), tee_connection_(std::move(tee_connection)) {}

  void OpenSession();
  [[nodiscard]] TEEC_Result InvokeSecmemCommand(uint32_t command,
                                                std::vector<uint8_t>* cmd_buffer_vec);

  uint32_t session_id_;
  fuchsia::tee::DeviceSyncPtr tee_connection_;
};

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SECMEM_SESSION_H_
