// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/mixer/output_formatter.h"

#include <limits>
#include <type_traits>

#include <fbl/algorithm.h>
#include <math.h>

#include "garnet/bin/media/audio_server/constants.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

// Template to produce destination samples from normalized samples.
template <typename DType, typename Enable = void>
class DstConverter;

// Converting audio between float and int is surprisingly controversial.
// (blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html etc. --
// search for "audio float int convert"). In choosing an internal pipeline that
// is float32, we can accomodate float32 Sources (such as an incoming stream
// from the render API, or an input device). Unfortunately, if the Destination
// (output device or outgoing stream destined for capture API) is NOT float32,
// then we cannot communicate the value of +1.0; we clamp these values in
// DstConverter::Convert). That said, the "practically clipping" value of +1.0
// is rare in WAV files, and other sources can easily reduce their input levels.
template <typename DType>
class DstConverter<
    DType, typename std::enable_if<std::is_same<DType, int16_t>::value>::type> {
 public:
  static inline constexpr DType Convert(float sample) {
    int32_t val = round(sample * 0x00008000);
    return fbl::clamp<int32_t>(val, std::numeric_limits<int16_t>::min(),
                               std::numeric_limits<int16_t>::max());
  }
};

template <typename DType>
class DstConverter<
    DType, typename std::enable_if<std::is_same<DType, uint8_t>::value>::type> {
 public:
  static inline constexpr DType Convert(float sample) {
    int32_t val = round(sample * 0x0080) + 0x0080;
    return static_cast<DType>(
        fbl::clamp<int32_t>(val, std::numeric_limits<uint8_t>::min(),
                            std::numeric_limits<uint8_t>::max()));
  }
};

template <typename DType>
class DstConverter<
    DType, typename std::enable_if<std::is_same<DType, float>::value>::type> {
 public:
  // This will emit +1.0 values, which are legal per WAV format custom.
  static inline constexpr DType Convert(float sample) {
    return fbl::clamp(sample, -1.0f, 1.0f);
  }
};

// Template to fill samples with silence based on sample type.
template <typename DType, typename Enable = void>
class SilenceMaker;

template <typename DType>
class SilenceMaker<
    DType, typename std::enable_if<std::is_same<DType, int16_t>::value ||
                                   std::is_same<DType, float>::value>::type> {
 public:
  static inline void Fill(void* dest, size_t samples) {
    // This works even if DType is float/double: per IEEE-754, all 0s == +0.0.
    ::memset(dest, 0, samples * sizeof(DType));
  }
};

template <typename DType>
class SilenceMaker<
    DType, typename std::enable_if<std::is_same<DType, uint8_t>::value>::type> {
 public:
  static inline void Fill(void* dest, size_t samples) {
    ::memset(dest, 0x80, samples * sizeof(DType));
  }
};

// A templated class which implements the ProduceOutput and FillWithSilence
// methods of OutputFormatter
template <typename DType>
class OutputFormatterImpl : public OutputFormatter {
 public:
  explicit OutputFormatterImpl(
      const fuchsia::media::AudioMediaTypeDetailsPtr& format)
      : OutputFormatter(format, sizeof(DType)) {}

  void ProduceOutput(const float* source, void* dest_void,
                     uint32_t frames) const override {
    using DC = DstConverter<DType>;
    DType* dest = static_cast<DType*>(dest_void);

    // Previously we clamped here; because of rounding, this is different for
    // each output type, so it is now handled in Convert() specializations.
    for (size_t i = 0; i < (static_cast<size_t>(frames) * channels_); ++i) {
      dest[i] = DC::Convert(source[i]);
    }
  }

  void FillWithSilence(void* dest, uint32_t frames) const override {
    SilenceMaker<DType>::Fill(dest, frames * channels_);
  }
};

// Constructor/destructor for the common OutputFormatter base class.
OutputFormatter::OutputFormatter(
    const fuchsia::media::AudioMediaTypeDetailsPtr& format,
    uint32_t bytes_per_sample)
    : channels_(format->channels),
      bytes_per_sample_(bytes_per_sample),
      bytes_per_frame_(bytes_per_sample * format->channels) {
  fidl::Clone(format, &format_);
}

// Selection routine which will instantiate a particular templatized version of
// the output formatter.
OutputFormatterPtr OutputFormatter::Select(
    const fuchsia::media::AudioMediaTypeDetailsPtr& format) {
  FXL_DCHECK(format);
  FXL_DCHECK(format->sample_format != fuchsia::media::AudioSampleFormat::ANY);
  FXL_DCHECK(format->sample_format != fuchsia::media::AudioSampleFormat::NONE);
  // MTWN-93: Consider eliminating these enums if we don't foresee using them.

  switch (format->sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return OutputFormatterPtr(new OutputFormatterImpl<uint8_t>(format));
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return OutputFormatterPtr(new OutputFormatterImpl<int16_t>(format));
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return OutputFormatterPtr(new OutputFormatterImpl<float>(format));
    default:
      FXL_LOG(ERROR) << "Unsupported output format "
                     << (uint32_t)format->sample_format;
      return nullptr;
  }
}

}  // namespace audio
}  // namespace media
