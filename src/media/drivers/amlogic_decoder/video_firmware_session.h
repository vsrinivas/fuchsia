// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/fit/result.h>

#include <optional>

#include <tee-client-api/tee-client-types.h>

class VideoFirmwareSession {
 public:
  static fit::result<VideoFirmwareSession, fuchsia::tee::DeviceSyncPtr> TryOpen(
      fuchsia::tee::DeviceSyncPtr tee_connection);

  VideoFirmwareSession(VideoFirmwareSession&&) = default;
  VideoFirmwareSession& operator=(VideoFirmwareSession&&) = default;

  ~VideoFirmwareSession();

  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmware(const uint8_t* data, uint32_t size);
  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmwareEncoder(uint8_t* data, uint32_t size);

 private:
  explicit VideoFirmwareSession(uint32_t session_id, fuchsia::tee::DeviceSyncPtr tee_connection)
      : session_id_(session_id), tee_connection_(std::move(tee_connection)) {}

  uint32_t session_id_;
  fuchsia::tee::DeviceSyncPtr tee_connection_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_
