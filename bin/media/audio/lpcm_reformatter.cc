// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/lpcm_reformatter.h"

#include "lib/fxl/logging.h"

namespace media {

// LpcmReformatter implementation that accepts samples of type TIn and
// produces samples of type TOut.
template <typename TIn, typename TOut>
class LpcmReformatterImpl : public LpcmReformatter {
 public:
  LpcmReformatterImpl(const AudioStreamType& in_type,
                      AudioStreamType::SampleFormat out_sample_format);

  ~LpcmReformatterImpl() override;

  // LpcmReformatter override.
  std::unique_ptr<StreamType> output_stream_type() override;

  // Transform implementation.
  bool TransformPacket(const PacketPtr& input,
                       bool new_input,
                       PayloadAllocator* allocator,
                       PacketPtr* output) override;

 private:
  AudioStreamType in_type_;
  AudioStreamType out_type_;
};

std::shared_ptr<LpcmReformatter> LpcmReformatter::Create(
    const AudioStreamType& in_type,
    const AudioStreamType::SampleFormat out_sample_format) {
  LpcmReformatter* result = nullptr;

  switch (in_type.sample_format()) {
    case AudioStreamType::SampleFormat::kUnsigned8:
      switch (out_sample_format) {
        case AudioStreamType::SampleFormat::kUnsigned8:
        case AudioStreamType::SampleFormat::kAny:
          result = new LpcmReformatterImpl<uint8_t, uint8_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned16:
          result = new LpcmReformatterImpl<uint8_t, int16_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned24In32:
          result = new LpcmReformatterImpl<uint8_t, int32_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kFloat:
          result = new LpcmReformatterImpl<uint8_t, float>(in_type,
                                                           out_sample_format);
          break;
        default:
          FXL_DCHECK(false) << "unsupported sample format";
          result = nullptr;
          break;
      }
      break;
    case AudioStreamType::SampleFormat::kSigned16:
      switch (out_sample_format) {
        case AudioStreamType::SampleFormat::kUnsigned8:
          result = new LpcmReformatterImpl<int16_t, uint8_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned16:
        case AudioStreamType::SampleFormat::kAny:
          result = new LpcmReformatterImpl<int16_t, int16_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned24In32:
          result = new LpcmReformatterImpl<int16_t, int32_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kFloat:
          result = new LpcmReformatterImpl<int16_t, float>(in_type,
                                                           out_sample_format);
          break;
        default:
          FXL_DCHECK(false) << "unsupported sample format";
          result = nullptr;
          break;
      }
      break;
    case AudioStreamType::SampleFormat::kSigned24In32:
      switch (out_sample_format) {
        case AudioStreamType::SampleFormat::kUnsigned8:
          result = new LpcmReformatterImpl<int32_t, uint8_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned16:
          result = new LpcmReformatterImpl<int32_t, int16_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned24In32:
        case AudioStreamType::SampleFormat::kAny:
          result = new LpcmReformatterImpl<int32_t, int32_t>(in_type,
                                                             out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kFloat:
          result = new LpcmReformatterImpl<int32_t, float>(in_type,
                                                           out_sample_format);
          break;
        default:
          FXL_DCHECK(false) << "unsupported sample format";
          result = nullptr;
          break;
      }
      break;
    case AudioStreamType::SampleFormat::kFloat:
      switch (out_sample_format) {
        case AudioStreamType::SampleFormat::kUnsigned8:
          result = new LpcmReformatterImpl<float, uint8_t>(in_type,
                                                           out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned16:
          result = new LpcmReformatterImpl<float, int16_t>(in_type,
                                                           out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kSigned24In32:
          result = new LpcmReformatterImpl<float, int32_t>(in_type,
                                                           out_sample_format);
          break;
        case AudioStreamType::SampleFormat::kFloat:
        case AudioStreamType::SampleFormat::kAny:
          result =
              new LpcmReformatterImpl<float, float>(in_type, out_sample_format);
          break;
        default:
          FXL_DCHECK(false) << "unsupported sample format";
          result = nullptr;
          break;
      }
      break;
    default:
      FXL_DCHECK(false) << "unsupported sample format";
      result = nullptr;
      break;
  }

  return std::shared_ptr<LpcmReformatter>(result);
}

template <typename TIn, typename TOut>
LpcmReformatterImpl<TIn, TOut>::LpcmReformatterImpl(
    const AudioStreamType& in_type,
    AudioStreamType::SampleFormat out_sample_format)
    : in_type_(in_type),
      out_type_(in_type.encoding(),
                nullptr,
                out_sample_format,
                in_type.channels(),
                in_type.frames_per_second()) {
  FXL_DCHECK(in_type.encoding() == StreamType::kAudioEncodingLpcm);
  FXL_DCHECK(in_type.encoding_parameters() == nullptr);
}

template <typename TIn, typename TOut>
LpcmReformatterImpl<TIn, TOut>::~LpcmReformatterImpl() {}

template <typename TIn, typename TOut>
std::unique_ptr<StreamType>
LpcmReformatterImpl<TIn, TOut>::output_stream_type() {
  return out_type_.Clone();
}

namespace {

template <typename T>
inline constexpr T Clamp(T val, T min, T max) {
  return (val > max) ? max : ((val < min) ? min : val);
}

template <typename T>
inline constexpr T Clamp(T val);

template <>
inline constexpr float Clamp(float val) {
  return Clamp(val, -1.0f, 1.0f);
}

template <>
inline constexpr int32_t Clamp(int32_t val) {
  return Clamp(val, 1 << 23, -(1 << 23));
}

template <typename TIn, typename TOut>
inline void CopySample(TOut* dest, const TIn* source) {
  *dest = static_cast<TOut>(*source);
}

inline void CopySample(uint8_t* dest, const int16_t* source) {
  *dest = static_cast<uint8_t>((*source >> 8) ^ 0x80);
}

inline void CopySample(uint8_t* dest, const int32_t* source) {
  *dest = static_cast<uint8_t>((Clamp(*source) >> 16) ^ 0x80);
}

inline void CopySample(uint8_t* dest, const float* source) {
  *dest = static_cast<uint8_t>((Clamp(*source) * 0x7f) + 128);
}

inline void CopySample(int16_t* dest, const uint8_t* source) {
  *dest = static_cast<int16_t>(*source ^ 0x80) << 8;
}

inline void CopySample(int16_t* dest, const int32_t* source) {
  *dest = static_cast<int16_t>(Clamp(*source) >> 8);
}

inline void CopySample(int16_t* dest, const float* source) {
  *dest = static_cast<int16_t>(Clamp(*source) * 0x7fff);
}

inline void CopySample(int32_t* dest, const uint8_t* source) {
  *dest = static_cast<int32_t>(*source ^ 0x80) << 16;
}

inline void CopySample(int32_t* dest, const int16_t* source) {
  *dest = static_cast<int32_t>(*source << 8);
}

inline void CopySample(int32_t* dest, const float* source) {
  *dest = static_cast<int32_t>(Clamp(*source) * 0x7fffff);
}

inline void CopySample(float* dest, const uint8_t* source) {
  *dest = static_cast<float>(*source ^ 0x80) / 0x80;
}

inline void CopySample(float* dest, const int16_t* source) {
  *dest = static_cast<float>(*source) / 0x8000;
}

inline void CopySample(float* dest, const int32_t* source) {
  *dest = static_cast<float>(Clamp(*source)) / 0x800000;
}

}  // namespace

template <typename TIn, typename TOut>
bool LpcmReformatterImpl<TIn, TOut>::TransformPacket(
    const PacketPtr& input,
    bool new_input,
    PayloadAllocator* allocator,
    PacketPtr* output) {
  FXL_DCHECK(input);
  FXL_DCHECK(allocator);
  FXL_DCHECK(output);

  uint64_t in_size = input->size();
  if (in_size == 0) {
    // Zero-sized input packet. Make a copy.
    *output = Packet::Create(input->pts(), input->pts_rate(), false,
                             input->end_of_stream(), 0, nullptr, nullptr);
    return true;
  }

  size_t frame_count = in_type_.frame_count(in_size);
  uint64_t out_size = out_type_.min_buffer_size(frame_count);

  void* buffer = allocator->AllocatePayloadBuffer(out_size);
  if (buffer == nullptr) {
    FXL_LOG(WARNING) << "lpcm reformatter starved for buffers";
    // Starved for buffer space. Can't process now.
    *output = nullptr;
    return false;
  }

  const TIn* in_channel = static_cast<const TIn*>(input->payload());
  TOut* out_channel = static_cast<TOut*>(buffer);
  uint32_t channels = in_type_.channels();

  for (uint32_t channel = 0; channel < in_type_.channels(); channel++) {
    const TIn* in_sample = in_channel;
    TOut* out_sample = out_channel;
    for (size_t sample = 0; sample < frame_count; sample++) {
      CopySample(out_sample, in_sample);
      in_sample += channels;
      out_sample += channels;
    }
    ++in_channel;
    ++out_channel;
  }

  *output = Packet::Create(input->pts(), input->pts_rate(), false,
                           input->end_of_stream(), out_size, buffer, allocator);

  return true;
}

}  // namespace media
