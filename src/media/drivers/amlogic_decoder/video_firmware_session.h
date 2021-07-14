// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_

#include <fuchsia/tee/cpp/fidl.h>
#include <lib/fpromise/result.h>

#include <optional>

#include <tee-client-api/tee-client-types.h>

namespace amlogic_decoder {

class VideoFirmwareSession {
 public:
  static fpromise::result<VideoFirmwareSession, fuchsia::tee::ApplicationSyncPtr> TryOpen(
      fuchsia::tee::ApplicationSyncPtr tee_connection);

  VideoFirmwareSession(VideoFirmwareSession&&) = default;
  VideoFirmwareSession& operator=(VideoFirmwareSession&&) = default;

  ~VideoFirmwareSession();

  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmware(const uint8_t* data, uint32_t size);
  // For now, any TEEC_Result != TEEC_SUCCESS returns ZX_ERR_INTERNAL.
  [[nodiscard]] zx_status_t LoadVideoFirmwareEncoder(uint8_t* data, uint32_t size);

 private:
  explicit VideoFirmwareSession(uint32_t session_id,
                                fuchsia::tee::ApplicationSyncPtr tee_connection)
      : session_id_(session_id), tee_connection_(std::move(tee_connection)) {}

  uint32_t session_id_;
  fuchsia::tee::ApplicationSyncPtr tee_connection_;
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_VIDEO_FIRMWARE_SESSION_H_
