// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/stream_converter.h"

#include <fidl/fuchsia.audio/cpp/natural_ostream.h>
#include <lib/syslog/cpp/macros.h>

#include <type_traits>

#include "src/media/audio/lib/format2/sample_converter.h"

namespace media_audio {

using SampleType = fuchsia_audio::SampleType;

class StreamConverter::CopyImpl {
 public:
  virtual ~CopyImpl() = default;
  virtual void Copy(const void* source_data, void* dest_data, int64_t frame_count) const = 0;
  virtual void CopyAndClip(const void* source_data, void* dest_data, int64_t frame_count) const = 0;
};

namespace {

class CopyWithMemcpy : public StreamConverter::CopyImpl {
 public:
  explicit CopyWithMemcpy(int64_t bytes_per_frame) : bytes_per_frame_(bytes_per_frame) {}

  void Copy(const void* source_data, void* dest_data, int64_t frame_count) const final {
    std::memmove(dest_data, source_data, frame_count * bytes_per_frame_);
  }

  void CopyAndClip(const void* source_data, void* dest_data, int64_t frame_count) const final {
    std::memmove(dest_data, source_data, frame_count * bytes_per_frame_);
  }

 private:
  const int64_t bytes_per_frame_;
};

template <typename SourceSampleType, typename DestSampleType>
class CopyWithConvert : public StreamConverter::CopyImpl {
 public:
  explicit CopyWithConvert(int64_t channels) : channels_(channels) {}

  void Copy(const void* source_data, void* dest_data, int64_t frame_count) const final {
    const auto sample_count = frame_count * channels_;

    auto source_ptr = static_cast<const SourceSampleType*>(source_data);
    auto dest_ptr = static_cast<DestSampleType*>(dest_data);

    for (int64_t k = 0; k < sample_count; k++) {
      using SourceConverter = SampleConverter<SourceSampleType>;
      using DestConverter = SampleConverter<DestSampleType>;
      dest_ptr[k] = DestConverter::FromFloat(SourceConverter::ToFloat(source_ptr[k]));
    }
  }

  void CopyAndClip(const void* source_data, void* dest_data, int64_t frame_count) const final {
    const auto sample_count = frame_count * channels_;

    auto source_ptr = static_cast<const SourceSampleType*>(source_data);
    auto dest_ptr = static_cast<DestSampleType*>(dest_data);

    for (int64_t k = 0; k < sample_count; k++) {
      using SourceConverter = SampleConverter<SourceSampleType>;
      using DestConverter = SampleConverter<DestSampleType>;
      auto sample = DestConverter::FromFloat(SourceConverter::ToFloat(source_ptr[k]));

      // For float->float, the final sample needs to be clamped.
      if constexpr (std::is_same_v<SourceSampleType, float> &&
                    std::is_same_v<DestSampleType, float>) {
        sample = std::clamp<float>(sample, -1.0f, 1.0f);
      }
      dest_ptr[k] = sample;
    }
  }

 private:
  const int64_t channels_;
};

template <typename SourceSampleType>
std::unique_ptr<StreamConverter::CopyImpl> CreateCopyWithConvert(const Format& dest) {
  switch (dest.sample_type()) {
    case SampleType::kUint8:
      if constexpr (std::is_same_v<SourceSampleType, uint8_t>) {
        __builtin_unreachable();  // should use memcpy
      }
      return std::make_unique<CopyWithConvert<SourceSampleType, uint8_t>>(dest.channels());

    case SampleType::kInt16:
      if constexpr (std::is_same_v<SourceSampleType, int16_t>) {
        __builtin_unreachable();  // should use memcpy
      }
      return std::make_unique<CopyWithConvert<SourceSampleType, int16_t>>(dest.channels());

    case SampleType::kInt32:
      if constexpr (std::is_same_v<SourceSampleType, int32_t>) {
        __builtin_unreachable();  // should use memcpy
      }
      return std::make_unique<CopyWithConvert<SourceSampleType, int32_t>>(dest.channels());

    case SampleType::kFloat32:
      return std::make_unique<CopyWithConvert<SourceSampleType, float>>(dest.channels());

    default:
      FX_LOGS(FATAL) << dest.sample_type();
      __builtin_unreachable();
  }
}

std::unique_ptr<StreamConverter::CopyImpl> CreateCopyImpl(const Format& source,
                                                          const Format& dest) {
  FX_CHECK(source.frames_per_second() == dest.frames_per_second());
  FX_CHECK(source.channels() == dest.channels());

  // If the formats are the same and don't require clamping, use a memcpy implementation.
  if (source.sample_type() == dest.sample_type() && source.sample_type() != SampleType::kFloat32) {
    return std::make_unique<CopyWithMemcpy>(source.bytes_per_frame());
  }

  // Otherwise use an implementation that does type conversion and clamping.
  switch (source.sample_type()) {
    case SampleType::kUint8:
      return CreateCopyWithConvert<uint8_t>(dest);
    case SampleType::kInt16:
      return CreateCopyWithConvert<int16_t>(dest);
    case SampleType::kInt32:
      return CreateCopyWithConvert<int32_t>(dest);
    case SampleType::kFloat32:
      return CreateCopyWithConvert<float>(dest);
    default:
      FX_LOGS(FATAL) << source.sample_type();
      __builtin_unreachable();
  }
}

}  // namespace

StreamConverter::StreamConverter(const Format& source_format, const Format& dest_format,
                                 std::unique_ptr<CopyImpl> copy_impl)
    : source_format_(source_format), dest_format_(dest_format), copy_impl_(std::move(copy_impl)) {}

// These must be defined after CopyImpl, which is defined in this file.
StreamConverter::~StreamConverter() = default;
StreamConverter::StreamConverter(StreamConverter&&) = default;
StreamConverter& StreamConverter::operator=(StreamConverter&&) = default;

// static
std::shared_ptr<StreamConverter> StreamConverter::Create(const Format& source_format,
                                                         const Format& dest_format) {
  return Create(source_format, dest_format, CreateCopyImpl(source_format, dest_format));
}

// static
std::shared_ptr<StreamConverter> StreamConverter::CreateFromFloatSource(const Format& dest_format) {
  auto source_format = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = dest_format.channels(),
      .frames_per_second = dest_format.frames_per_second(),
  });
  return Create(source_format, dest_format, CreateCopyWithConvert<float>(dest_format));
}

// static
std::shared_ptr<StreamConverter> StreamConverter::Create(const Format& source_format,
                                                         const Format& dest_format,
                                                         std::unique_ptr<CopyImpl> copy_impl) {
  struct WithPublicCtor : public StreamConverter {
    WithPublicCtor(const Format& source_format, const Format& dest_format,
                   std::unique_ptr<CopyImpl> copy_impl)
        : StreamConverter(source_format, dest_format, std::move(copy_impl)) {}
  };

  return std::make_shared<WithPublicCtor>(source_format, dest_format, std::move(copy_impl));
}

void StreamConverter::Copy(const void* source_data, void* dest_data, int64_t frame_count) const {
  copy_impl_->Copy(source_data, dest_data, frame_count);
}

void StreamConverter::CopyAndClip(const void* source_data, void* dest_data,
                                  int64_t frame_count) const {
  copy_impl_->CopyAndClip(source_data, dest_data, frame_count);
}

void StreamConverter::WriteSilence(void* dest_data, int64_t frame_count) const {
  if (dest_format().sample_type() == SampleType::kUint8) {
    std::memset(dest_data, kInt8ToUint8, frame_count * dest_format().channels());
  } else {
    // All other sample formats represent silence with zeroes.
    std::memset(dest_data, 0, frame_count * dest_format().bytes_per_frame());
  }
}

}  // namespace media_audio
