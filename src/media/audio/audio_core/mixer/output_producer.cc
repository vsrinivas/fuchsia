// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/output_producer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include <trace/event.h>

#include "lib/fidl/cpp/clone.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/mixer/constants.h"

namespace media::audio {

// Converting audio between float and int is surprisingly controversial.
// (blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html etc. -- web-search "audio
// float int convert"). Our float32-based internal pipeline can accommodate float and int Sources
// without data loss (where Source is a client-submitted stream from AudioRenderer, or an input
// device), but for non-float Destinations (output device, or AudioCapturer stream to a client) we
// must clamp +1.0 values in DestConverter::Convert. When translating from float to int16 for
// example, we can translate -1.0 perfectly to -32768 (negative 0x8000), while +1.0 cannot become
// +32768 (positive 0x8000, exceeding int16's max) so it is clamped to 32767 (0x7FFF).
//
// Having said all this, the "practically clipping" value of +1.0 is rare in WAV
// files, and other sources should easily be able to reduce their input levels.

// Template to produce destination samples from normalized samples.
template <typename DType, typename Enable = void>
class DestConverter;

template <typename DType>
class DestConverter<DType, typename std::enable_if_t<std::is_same_v<DType, uint8_t>>> {
 public:
  static inline constexpr DType Convert(float sample) {
    return std::clamp<int32_t>(round(sample * kFloatToInt8), std::numeric_limits<int8_t>::min(),
                               std::numeric_limits<int8_t>::max()) +
           kOffsetInt8ToUint8;
  }
};

template <typename DType>
class DestConverter<DType, typename std::enable_if_t<std::is_same_v<DType, int16_t>>> {
 public:
  static inline constexpr DType Convert(float sample) {
    return std::clamp<int32_t>(round(sample * kFloatToInt16), std::numeric_limits<int16_t>::min(),
                               std::numeric_limits<int16_t>::max());
  }
};

template <typename DType>
class DestConverter<DType, typename std::enable_if_t<std::is_same_v<DType, int32_t>>> {
 public:
  static inline constexpr DType Convert(float sample) {
    return std::clamp<int64_t>(round(sample * kFloatToInt24In32), kMinInt24In32, kMaxInt24In32);
  }
};

template <typename DType>
class DestConverter<DType, typename std::enable_if_t<std::is_same_v<DType, float>>> {
 public:
  // This will emit +1.0 values, which are legal per WAV format custom.
  static inline constexpr DType Convert(float sample) { return std::clamp(sample, -1.0f, 1.0f); }
};

// Template to fill samples with silence based on sample type.
template <typename DType, typename Enable = void>
class SilenceMaker;

template <typename DType>
class SilenceMaker<DType, typename std::enable_if_t<std::is_same_v<DType, int16_t> ||
                                                    std::is_same_v<DType, int32_t> ||
                                                    std::is_same_v<DType, float>>> {
 public:
  static inline void Fill(void* dest, size_t samples) {
    // This works even if DType is float/double: per IEEE-754, all 0s == +0.0.
    memset(dest, 0, samples * sizeof(DType));
  }
};

template <typename DType>
class SilenceMaker<DType, typename std::enable_if_t<std::is_same_v<DType, uint8_t>>> {
 public:
  static inline void Fill(void* dest, size_t samples) {
    memset(dest, kOffsetInt8ToUint8, samples * sizeof(DType));
  }
};

// Templated class that implements ProduceOutput and FillWithSilence methods for OutputProducer
template <typename DType>
class OutputProducerImpl : public OutputProducer {
 public:
  explicit OutputProducerImpl(const fuchsia::media::AudioStreamType& format)
      : OutputProducer(format, sizeof(DType)) {}

  void ProduceOutput(const float* source, void* dest_void, uint32_t frames) const override {
    TRACE_DURATION("audio", "OutputProducerImpl::ProduceOutput");
    using DC = DestConverter<DType>;
    auto* dest = static_cast<DType*>(dest_void);

    // Previously we clamped here; because of rounding, this is different for
    // each output type, so it is now handled in Convert() specializations.
    for (size_t i = 0; i < (static_cast<size_t>(frames) * channels_); ++i) {
      dest[i] = DC::Convert(source[i]);
    }
  }

  void FillWithSilence(void* dest, uint32_t frames) const override {
    TRACE_DURATION("audio", "OutputProducerImpl::FillWithSilence");
    SilenceMaker<DType>::Fill(dest, frames * channels_);
  }
};

// Constructor/destructor for the common OutputProducer base class.
OutputProducer::OutputProducer(const fuchsia::media::AudioStreamType& format,
                               uint32_t bytes_per_sample)
    : channels_(format.channels),
      bytes_per_sample_(bytes_per_sample),
      bytes_per_frame_(bytes_per_sample * format.channels) {
  fidl::Clone(format, &format_);
}

// Selection routine which will instantiate a particular templatized version of the output producer.
std::unique_ptr<OutputProducer> OutputProducer::Select(
    const fuchsia::media::AudioStreamType& format) {
  TRACE_DURATION("audio", "OutputProducer::Select");
  if (format.channels == 0u) {
    FX_LOGS(ERROR) << "Invalid output format";
    return nullptr;
  }

  switch (format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return std::make_unique<OutputProducerImpl<uint8_t>>(format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return std::make_unique<OutputProducerImpl<int16_t>>(format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return std::make_unique<OutputProducerImpl<int32_t>>(format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return std::make_unique<OutputProducerImpl<float>>(format);
    default:
      FX_LOGS(ERROR) << "Unsupported output format " << (uint32_t)format.sample_format;
      return nullptr;
  }
}

}  // namespace media::audio
