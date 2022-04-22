// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_

namespace media {
class VaapiWrapper {
 public:
  bool UploadVideoFrameToSurface(media::VideoFrame& frame, VASurfaceID input_surface_id,
                                 const gfx::Size& input_surface_size) {
    return false;
  }
  bool ExecuteAndDestroyPendingBuffers(VASurfaceID surface_id) { return false; }
  uint64_t GetEncodedChunkSize(VABufferID buffer_id, VASurfaceID surface_id) { return 0; }
  bool GetSupportedPackedHeaders(media::VideoCodecProfile profile, bool& packed_sps,
                                 bool& packed_pps, bool& packed_slice) {
    return false;
  }

  bool SubmitBuffer(VABufferType va_buffer_type, size_t size, const void* data) { return false; }

  template <typename T>
  [[nodiscard]] bool SubmitBuffer(VABufferType va_buffer_type, const T* data) {
    return SubmitBuffer(va_buffer_type, sizeof(T), data);
  }
};

}  // namespace media

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_WRAPPER_H_
