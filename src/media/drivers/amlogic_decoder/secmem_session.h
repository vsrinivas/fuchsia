// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_SESSION_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_SESSION_H_

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

  // The naming is for consistency with the TA command name, but this actually adds AMLV headers
  // to each VP9 frame (adds header to the one frame, or to all frames within a superframe).
  //
  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t GetVp9HeaderSize(zx_paddr_t vp9_paddr, uint32_t before_size,
                                             uint32_t max_after_size, uint32_t* after_size);

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

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_SESSION_H_
