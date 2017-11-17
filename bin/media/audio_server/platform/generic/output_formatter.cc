// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"

#include <limits>
#include <type_traits>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

// Template to produce destination samples from normalized samples.
template <typename DType, typename Enable = void>
class DstConverter;

template <typename DType>
class DstConverter<
    DType,
    typename std::enable_if<std::is_same<DType, int16_t>::value, void>::type> {
 public:
  static inline constexpr DType Convert(int32_t sample) {
    return static_cast<DType>(sample);
  }
};

template <typename DType>
class DstConverter<
    DType,
    typename std::enable_if<std::is_same<DType, uint8_t>::value, void>::type> {
 public:
  static inline constexpr DType Convert(int32_t sample) {
    // Convert to signed, round, reduce to 8-bit ==> +0x8000, +0x0080, >>8
    return static_cast<DType>((sample + 0x8080) >> 8);
  }
};

// Template to fill samples with silence based on sample type.
template <typename DType, typename Enable = void>
class SilenceMaker;

template <typename DType>
class SilenceMaker<
    DType,
    typename std::enable_if<std::is_same<DType, int16_t>::value, void>::type> {
 public:
  static inline void Fill(void* dest, size_t samples) {
    ::memset(dest, 0, samples * sizeof(DType));
  }
};

template <typename DType>
class SilenceMaker<
    DType,
    typename std::enable_if<std::is_same<DType, uint8_t>::value, void>::type> {
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
  explicit OutputFormatterImpl(const AudioMediaTypeDetailsPtr& format)
      : OutputFormatter(format, sizeof(DType)) {}

  void ProduceOutput(const int32_t* source,
                     void* dest_void,
                     uint32_t frames) const override {
    using DC = DstConverter<DType>;
    DType* dest = static_cast<DType*>(dest_void);

    for (size_t i = 0; i < (static_cast<size_t>(frames) * channels_); ++i) {
      int32_t val = source[i];
      if (val > std::numeric_limits<int16_t>::max()) {
        dest[i] = DC::Convert(std::numeric_limits<int16_t>::max());
      } else if (val < std::numeric_limits<int16_t>::min()) {
        dest[i] = DC::Convert(std::numeric_limits<int16_t>::min());
      } else {
        dest[i] = DC::Convert(val);
      }
    }
  }

  void FillWithSilence(void* dest, uint32_t frames) const override {
    SilenceMaker<DType>::Fill(dest, frames * channels_);
  }
};

// Constructor/destructor for the common OutputFormatter base class.
OutputFormatter::OutputFormatter(const AudioMediaTypeDetailsPtr& format,
                                 uint32_t bytes_per_sample)
    : format_(format.Clone()),
      channels_(format->channels),
      bytes_per_sample_(bytes_per_sample),
      bytes_per_frame_(bytes_per_sample * format->channels) {}

// Selection routine which will instantiate a particular templatized version of
// the output formatter.
OutputFormatterPtr OutputFormatter::Select(
    const AudioMediaTypeDetailsPtr& format) {
  FXL_DCHECK(format);

  switch (format->sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      return OutputFormatterPtr(new OutputFormatterImpl<uint8_t>(format));
    case AudioSampleFormat::SIGNED_16:
      return OutputFormatterPtr(new OutputFormatterImpl<int16_t>(format));
    default:
      FXL_LOG(ERROR) << "Unsupported output sample format "
                     << format->sample_format;
      return nullptr;
  }
}

}  // namespace audio
}  // namespace media
