// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/lpcm_util.h"

#include "garnet/bin/mediaplayer/framework/formatting.h"
#include "lib/fxl/logging.h"

namespace media_player {

// LpcmUtil implementation that processes samples of type T.
template <typename T>
class LpcmUtilImpl : public LpcmUtil {
 public:
  ~LpcmUtilImpl();

  void Silence(void* buffer, size_t frame_count) const override;

  void Copy(const void* in, void* out, size_t frame_count) const override;

  void Mix(const void* in, void* out, size_t frame_count) const override;

  void Interleave(const void* in, size_t in_byte_count, void* out,
                  size_t frame_count) const override;

 private:
  LpcmUtilImpl(const AudioStreamType& stream_type);

  AudioStreamType stream_type_;

  friend class LpcmUtil;
};

std::unique_ptr<LpcmUtil> LpcmUtil::Create(const AudioStreamType& stream_type) {
  LpcmUtil* result;
  switch (stream_type.sample_format()) {
    case AudioStreamType::SampleFormat::kUnsigned8:
      result = new LpcmUtilImpl<uint8_t>(stream_type);
      break;
    case AudioStreamType::SampleFormat::kSigned16:
      result = new LpcmUtilImpl<int16_t>(stream_type);
      break;
    case AudioStreamType::SampleFormat::kSigned24In32:
      result = new LpcmUtilImpl<int32_t>(stream_type);
      break;
    case AudioStreamType::SampleFormat::kFloat:
      result = new LpcmUtilImpl<float>(stream_type);
      break;
    default:
      FXL_DCHECK(false) << "unsupported sample format "
                        << stream_type.sample_format();
      result = nullptr;
      break;
  }

  return std::unique_ptr<LpcmUtil>(result);
}

template <typename T>
LpcmUtilImpl<T>::LpcmUtilImpl(const AudioStreamType& stream_type)
    : stream_type_(stream_type) {}

template <typename T>
LpcmUtilImpl<T>::~LpcmUtilImpl() {}

template <typename T>
void LpcmUtilImpl<T>::Silence(void* buffer, size_t frame_count) const {
  T* sample = reinterpret_cast<T*>(buffer);
  for (size_t sample_countdown = frame_count * stream_type_.channels();
       sample_countdown != 0; --sample_countdown) {
    *sample = 0;
    sample++;
  }
}

template <>
void LpcmUtilImpl<uint8_t>::Silence(void* buffer, size_t frame_count) const {
  std::memset(buffer, 0x80, frame_count * stream_type_.bytes_per_frame());
}

template <>
void LpcmUtilImpl<int16_t>::Silence(void* buffer, size_t frame_count) const {
  std::memset(buffer, 0, frame_count * stream_type_.bytes_per_frame());
}

template <>
void LpcmUtilImpl<int32_t>::Silence(void* buffer, size_t frame_count) const {
  std::memset(buffer, 0, frame_count * stream_type_.bytes_per_frame());
}

template <typename T>
void LpcmUtilImpl<T>::Copy(const void* in, void* out,
                           size_t frame_count) const {
  std::memcpy(out, in, stream_type_.min_buffer_size(frame_count));
}

template <typename T>
void LpcmUtilImpl<T>::Mix(const void* in, void* out, size_t frame_count) const {
  const T* in_sample = reinterpret_cast<const T*>(in);
  T* out_sample = reinterpret_cast<T*>(out);
  for (size_t sample_countdown = frame_count * stream_type_.channels();
       sample_countdown != 0; --sample_countdown) {
    *out_sample += *in_sample;  // TODO(dalesat): Limit.
    out_sample++;
    in_sample++;
  }
}

template <>
void LpcmUtilImpl<uint8_t>::Mix(const void* in, void* out,
                                size_t frame_count) const {
  const uint8_t* in_sample = reinterpret_cast<const uint8_t*>(in);
  uint8_t* out_sample = reinterpret_cast<uint8_t*>(out);
  for (size_t sample_countdown = frame_count * stream_type_.channels();
       sample_countdown != 0; --sample_countdown) {
    *out_sample = uint8_t(uint16_t(*out_sample) + uint16_t(*in_sample) - 0x80);
    // TODO(dalesat): Limit.
    out_sample++;
    in_sample++;
  }
}

template <typename T>
void LpcmUtilImpl<T>::Interleave(const void* in, size_t in_byte_count,
                                 void* out, size_t frame_count) const {
  FXL_DCHECK(in);
  FXL_DCHECK(in_byte_count);
  FXL_DCHECK(out);
  FXL_DCHECK(frame_count);

  uint32_t channels = stream_type_.channels();
  FXL_DCHECK(channels);
  FXL_DCHECK(in_byte_count % stream_type_.bytes_per_frame() == 0);
  FXL_DCHECK(in_byte_count >= frame_count * stream_type_.bytes_per_frame());
  uint64_t in_channel_stride = in_byte_count / stream_type_.bytes_per_frame();

  const T* in_channel = reinterpret_cast<const T*>(in);
  T* out_channel = reinterpret_cast<T*>(out);

  for (uint32_t channel = channels; channel != 0; --channel) {
    const T* in_sample = in_channel;
    T* out_sample = out_channel;
    for (uint64_t frame = frame_count; frame != 0; --frame) {
      *out_sample = *in_sample;
      ++in_sample;
      out_sample += channels;
    }
    in_channel += in_channel_stride;
    ++out_channel;
  }
}

}  // namespace media_player
