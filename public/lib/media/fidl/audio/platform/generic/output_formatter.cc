// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/audio/platform/generic/output_formatter.h"

#include <limits>
#include <type_traits>

#include "lib/ftl/logging.h"

namespace mojo {
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
    return static_cast<DType>((sample >> 8) + 0x80);
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
template <typename DType, uint32_t DChCount>
class OutputFormatterImpl : public OutputFormatter {
 public:
  explicit OutputFormatterImpl(const AudioMediaTypeDetailsPtr& format)
      : OutputFormatter(format, sizeof(DType), DChCount) {}

  void ProduceOutput(const int32_t* source,
                     void* dest_void,
                     uint32_t frames) const override {
    using DC = DstConverter<DType>;
    DType* dest = static_cast<DType*>(dest_void);

    for (size_t i = 0; i < (static_cast<size_t>(frames) * DChCount); ++i) {
      register int32_t val = source[i];
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
    SilenceMaker<DType>::Fill(dest, frames * DChCount);
  }
};

// Constructor/destructor for the common OutputFormatter base class.
OutputFormatter::OutputFormatter(const AudioMediaTypeDetailsPtr& format,
                                 uint32_t bytes_per_sample,
                                 uint32_t channels)
    : format_(format.Clone()),
      channels_(channels),
      bytes_per_sample_(bytes_per_sample),
      bytes_per_frame_(bytes_per_sample * channels) {}

OutputFormatter::~OutputFormatter() {}

// Selection routines which will instantiate a particular templatized version of
// the output formatter.
template <typename DType>
static inline OutputFormatterPtr SelectOF(
    const AudioMediaTypeDetailsPtr& format) {
  switch (format->channels) {
    case 1:
      return OutputFormatterPtr(new OutputFormatterImpl<DType, 1>(format));
    case 2:
      return OutputFormatterPtr(new OutputFormatterImpl<DType, 2>(format));
    default:
      FTL_LOG(ERROR) << "Unsupported output channels " << format->channels;
      return nullptr;
  }
}

OutputFormatterPtr OutputFormatter::Select(
    const AudioMediaTypeDetailsPtr& format) {
  FTL_DCHECK(format);

  switch (format->sample_format) {
    case AudioSampleFormat::UNSIGNED_8:
      return SelectOF<uint8_t>(format);
    case AudioSampleFormat::SIGNED_16:
      return SelectOF<int16_t>(format);
    default:
      FTL_LOG(ERROR) << "Unsupported output sample format "
                     << format->sample_format;
      return nullptr;
  }
}

}  // namespace audio
}  // namespace media
}  // namespace mojo
