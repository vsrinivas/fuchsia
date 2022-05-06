// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/output_producer.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include "src/media/audio/lib/format2/sample_converter.h"

namespace media::audio {

// Template to fill samples with silence based on sample type.
template <typename DType, typename Enable = void>
class SilenceMaker;

template <typename DType>
class SilenceMaker<DType, typename std::enable_if_t<std::is_same_v<DType, int16_t> ||
                                                    std::is_same_v<DType, int32_t> ||
                                                    std::is_same_v<DType, float>>> {
 public:
  static inline void Fill(void* dest_void_ptr, size_t samples) {
    // This works even if DType is float/double: per IEEE-754, all 0s == +0.0.
    memset(dest_void_ptr, 0, samples * sizeof(DType));
  }
};

template <typename DType>
class SilenceMaker<DType, typename std::enable_if_t<std::is_same_v<DType, uint8_t>>> {
 public:
  static inline void Fill(void* dest_void_ptr, size_t samples) {
    memset(dest_void_ptr, media_audio::kInt8ToUint8, samples * sizeof(DType));
  }
};

// Templated class that implements ProduceOutput and FillWithSilence methods for OutputProducer
template <typename DType>
class OutputProducerImpl : public OutputProducer {
 public:
  explicit OutputProducerImpl(const fuchsia::media::AudioStreamType& format)
      : OutputProducer(format, sizeof(DType)) {}

  void ProduceOutput(const float* source_ptr, void* dest_void_ptr, int64_t frames) const override {
    TRACE_DURATION("audio", "OutputProducerImpl::ProduceOutput");
    auto* dest_ptr = static_cast<DType*>(dest_void_ptr);

    // Previously we clamped here; because of rounding, this is different for
    // each output type, so it is now handled in Convert() specializations.
    const size_t kNumSamples = frames * channels_;
    for (size_t i = 0; i < kNumSamples; ++i) {
      dest_ptr[i] = media_audio::SampleConverter<DType>::FromFloat(source_ptr[i]);
    }
  }

  void FillWithSilence(void* dest_void_ptr, int64_t frames) const override {
    TRACE_DURATION("audio", "OutputProducerImpl::FillWithSilence");
    SilenceMaker<DType>::Fill(dest_void_ptr, frames * channels_);
  }
};

// Constructor/destructor for the common OutputProducer base class.
OutputProducer::OutputProducer(const fuchsia::media::AudioStreamType& format,
                               int32_t bytes_per_sample)
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
      FX_LOGS(ERROR) << "Unsupported output format " << (int64_t)format.sample_format;
      return nullptr;
  }
}

}  // namespace media::audio
