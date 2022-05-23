// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_

#include <va/va.h>

#include "media/video/video_encode_accelerator.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"
#include "src/media/third_party/chromium_media/chromium_utils.h"
#include "src/media/third_party/chromium_media/geometry.h"

namespace media {

class VaapiWrapper {
 public:
  bool UploadVideoFrameToSurface(media::VideoFrame& frame, VASurfaceID input_surface_id,
                                 const gfx::Size& input_surface_size);
  bool ExecuteAndDestroyPendingBuffers(VASurfaceID surface_id);
  uint64_t GetEncodedChunkSize(VABufferID buffer_id, VASurfaceID surface_id);
  bool GetSupportedPackedHeaders(media::VideoCodecProfile profile, bool& packed_sps,
                                 bool& packed_pps, bool& packed_slice);

  bool SubmitBuffer(VABufferType va_buffer_type, size_t size, const void* data);

  template <typename T>
  [[nodiscard]] bool SubmitBuffer(VABufferType va_buffer_type, const T* data) {
    return SubmitBuffer(va_buffer_type, sizeof(T), data);
  }

  void set_context_id(VAContextID context_id) { context_id_ = context_id; }

 private:
  VAContextID context_id_{};
  std::vector<ScopedBufferID> buffer_ids_;
};

}  // namespace media

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_
