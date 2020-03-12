// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_USAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_USAGE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/enum.h>

#include <type_traits>
#include <unordered_set>
#include <variant>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
namespace internal {

struct EnumHash {
  template <typename T>
  size_t operator()(T t) const {
    return static_cast<size_t>(t);
  }
};

}  // namespace internal

static_assert(fuchsia::media::RENDER_USAGE_COUNT == 5);
#define EXPAND_EACH_FIDL_RENDER_USAGE \
  EXPAND_RENDER_USAGE(BACKGROUND)     \
  EXPAND_RENDER_USAGE(MEDIA)          \
  EXPAND_RENDER_USAGE(INTERRUPTION)   \
  EXPAND_RENDER_USAGE(SYSTEM_AGENT)   \
  EXPAND_RENDER_USAGE(COMMUNICATION)

static constexpr uint32_t kStreamInternalRenderUsageCount = 1;
#define EXPAND_EACH_INTERNAL_RENDER_USAGE EXPAND_RENDER_USAGE(ULTRASOUND)

static_assert(fuchsia::media::CAPTURE_USAGE_COUNT == 4);
#define EXPAND_EACH_FIDL_CAPTURE_USAGE \
  EXPAND_CAPTURE_USAGE(BACKGROUND)     \
  EXPAND_CAPTURE_USAGE(FOREGROUND)     \
  EXPAND_CAPTURE_USAGE(SYSTEM_AGENT)   \
  EXPAND_CAPTURE_USAGE(COMMUNICATION)

static constexpr uint32_t kStreamInternalCaptureUsageCount = 1;
#define EXPAND_EACH_INTERNAL_CAPTURE_USAGE EXPAND_CAPTURE_USAGE(ULTRASOUND)

static constexpr uint32_t kStreamRenderUsageCount =
    fuchsia::media::RENDER_USAGE_COUNT + kStreamInternalRenderUsageCount;
enum class RenderUsage : std::underlying_type_t<fuchsia::media::AudioRenderUsage> {
#define EXPAND_RENDER_USAGE(U) U = fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::U),
  EXPAND_EACH_FIDL_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
#define EXPAND_RENDER_USAGE(U) U,
      EXPAND_EACH_INTERNAL_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
};

static constexpr uint32_t kStreamCaptureUsageCount =
    fuchsia::media::CAPTURE_USAGE_COUNT + kStreamInternalCaptureUsageCount;
enum class CaptureUsage : std::underlying_type_t<fuchsia::media::AudioCaptureUsage> {
#define EXPAND_CAPTURE_USAGE(U) U = fidl::ToUnderlying(fuchsia::media::AudioCaptureUsage::U),
  EXPAND_EACH_FIDL_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
#define EXPAND_CAPTURE_USAGE(U) U,
      EXPAND_EACH_INTERNAL_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
};

// Since we define the RenderUsage enum to have the same numeric values for each fidl enum entry,
// we can convert by casting the underlying numeric value.
static RenderUsage RenderUsageFromFidlRenderUsage(fuchsia::media::AudioRenderUsage u) {
  return static_cast<RenderUsage>(fidl::ToUnderlying(u));
}
static CaptureUsage CaptureUsageFromFidlCaptureUsage(fuchsia::media::AudioCaptureUsage u) {
  return static_cast<CaptureUsage>(fidl::ToUnderlying(u));
}

[[maybe_unused]] static std::optional<fuchsia::media::AudioRenderUsage>
FidlRenderUsageFromRenderUsage(RenderUsage u) {
  auto underlying = static_cast<std::underlying_type_t<RenderUsage>>(u);
  if (underlying < fuchsia::media::RENDER_USAGE_COUNT) {
    return {static_cast<fuchsia::media::AudioRenderUsage>(underlying)};
  }
  return {};
}
[[maybe_unused]] static std::optional<fuchsia::media::AudioCaptureUsage>
FidlCaptureUsageFromCaptureUsage(CaptureUsage u) {
  auto underlying = static_cast<std::underlying_type_t<CaptureUsage>>(u);
  if (underlying < fuchsia::media::CAPTURE_USAGE_COUNT) {
    return {static_cast<fuchsia::media::AudioCaptureUsage>(underlying)};
  }
  return {};
}

using RenderUsageSet = std::unordered_set<RenderUsage, internal::EnumHash>;
using CaptureUsageSet = std::unordered_set<CaptureUsage, internal::EnumHash>;

class StreamUsage {
 public:
  static StreamUsage WithRenderUsage(RenderUsage u) { return StreamUsage(u); }
  static StreamUsage WithCaptureUsage(CaptureUsage u) { return StreamUsage(u); }
  static StreamUsage WithRenderUsage(fuchsia::media::AudioRenderUsage u) {
    return StreamUsage(RenderUsageFromFidlRenderUsage(u));
  }
  static StreamUsage WithCaptureUsage(fuchsia::media::AudioCaptureUsage u) {
    return StreamUsage(CaptureUsageFromFidlCaptureUsage(u));
  }

  StreamUsage() = default;

  bool operator==(const StreamUsage& other) const { return usage_ == other.usage_; }
  bool operator!=(const StreamUsage& other) const { return !(*this == other); }

  // RenderUsage
  bool is_render_usage() const { return std::holds_alternative<RenderUsage>(usage_); }
  StreamUsage& set_render_usage(RenderUsage usage) {
    usage_ = usage;
    return *this;
  }
  RenderUsage render_usage() const {
    FX_DCHECK(is_render_usage());
    return std::get<RenderUsage>(usage_);
  }

  // CaptureUsage
  bool is_capture_usage() const { return std::holds_alternative<CaptureUsage>(usage_); }
  StreamUsage& set_capture_usage(CaptureUsage usage) {
    usage_ = usage;
    return *this;
  }
  CaptureUsage capture_usage() const {
    FX_DCHECK(is_capture_usage());
    return std::get<CaptureUsage>(usage_);
  }

 private:
  using Usage = std::variant<std::monostate, RenderUsage, CaptureUsage>;

  explicit StreamUsage(RenderUsage usage) : usage_(usage) {}
  explicit StreamUsage(CaptureUsage usage) : usage_(usage) {}

  Usage usage_;
};

}  // namespace media::audio

#undef EXPAND_EACH_FIDL_CAPTURE_USAGE
#undef EXPAND_EACH_FIDL_RENDER_USAGE
#undef EXPAND_EACH_INTERNAL_CAPTURE_USAGE
#undef EXPAND_EACH_INTERNAL_RENDER_USAGE

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_USAGE_H_
