// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_CLIENT_SESSION_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_CLIENT_SESSION_H_

#include <optional>

#include <tee-client-api/tee_client_api.h>

class SecmemClientSession {
 public:
  explicit SecmemClientSession(TEEC_Context* context);
  ~SecmemClientSession();

  [[nodiscard]] zx_status_t Init();

  // The naming is for consistency with the TA command name, but this actually adds AMLV headers
  // to each VP9 frame (adds header to the one frame, or to all frames within a superframe).
  //
  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t GetVp9HeaderSize(zx_paddr_t vp9_paddr, uint32_t before_size,
                                             uint32_t max_after_size, uint32_t* after_size);

 private:
  void PackUint32Parameter(uint32_t value, size_t* offset_in_out);
  [[nodiscard]] TEEC_Result InvokeSecmemCommand(uint32_t command, size_t length);
  [[nodiscard]] bool UnpackUint32Parameter(uint32_t* value, size_t* offset_in_out);

  TEEC_Context* context_ = {};
  std::optional<TEEC_Session> session_ = {};
  std::optional<TEEC_SharedMemory> parameter_buffer_ = {};
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_SECMEM_CLIENT_SESSION_H_
