// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include <tee-client-api/tee_client_api.h>

class VideoFirmwareSession {
 public:
  explicit VideoFirmwareSession(TEEC_Context* context);
  ~VideoFirmwareSession();

  [[nodiscard]] zx_status_t Init();

  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmware(uint8_t* data, uint32_t size);
  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmwareEncoder(uint8_t* data, uint32_t size);

 private:
  TEEC_Context* context_ = {};
  std::optional<TEEC_Session> session_ = {};
};
