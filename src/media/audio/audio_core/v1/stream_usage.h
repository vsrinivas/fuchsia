// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STREAM_USAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STREAM_USAGE_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/enum.h>
#include <lib/syslog/cpp/macros.h>

#include <iterator>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace media::audio {

static_assert(fuchsia::media::RENDER_USAGE_COUNT == 5);
#define EXPAND_EACH_FIDL_RENDER_USAGE \
  EXPAND_RENDER_USAGE(BACKGROUND)     \
  EXPAND_RENDER_USAGE(MEDIA)          \
  EXPAND_RENDER_USAGE(INTERRUPTION)   \
  EXPAND_RENDER_USAGE(SYSTEM_AGENT)   \
  EXPAND_RENDER_USAGE(COMMUNICATION)

static constexpr uint32_t kStreamInternalRenderUsageCount = 1;
#define EXPAND_EACH_INTERNAL_RENDER_USAGE EXPAND_RENDER_USAGE(ULTRASOUND)

#define EXPAND_EACH_RENDER_USAGE \
  EXPAND_EACH_FIDL_RENDER_USAGE  \
  EXPAND_EACH_INTERNAL_RENDER_USAGE

static_assert(fuchsia::media::CAPTURE_USAGE_COUNT == 4);
#define EXPAND_EACH_FIDL_CAPTURE_USAGE \
  EXPAND_CAPTURE_USAGE(BACKGROUND)     \
  EXPAND_CAPTURE_USAGE(FOREGROUND)     \
  EXPAND_CAPTURE_USAGE(SYSTEM_AGENT)   \
  EXPAND_CAPTURE_USAGE(COMMUNICATION)

static constexpr uint32_t kStreamInternalCaptureUsageCount = 2;
#define EXPAND_EACH_INTERNAL_CAPTURE_USAGE \
  EXPAND_CAPTURE_USAGE(LOOPBACK)           \
  EXPAND_CAPTURE_USAGE(ULTRASOUND)

#define EXPAND_EACH_CAPTURE_USAGE \
  EXPAND_EACH_FIDL_CAPTURE_USAGE  \
  EXPAND_EACH_INTERNAL_CAPTURE_USAGE

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

constexpr std::array<RenderUsage, fuchsia::media::RENDER_USAGE_COUNT> kFidlRenderUsages = {{
#define EXPAND_RENDER_USAGE(U) RenderUsage::U,
    EXPAND_EACH_FIDL_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
}};

constexpr std::array<RenderUsage, kStreamRenderUsageCount> kRenderUsages = {{
#define EXPAND_RENDER_USAGE(U) RenderUsage::U,
    EXPAND_EACH_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
}};

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

constexpr std::array<CaptureUsage, fuchsia::media::CAPTURE_USAGE_COUNT> kFidlCaptureUsages = {{
#define EXPAND_CAPTURE_USAGE(U) CaptureUsage::U,
    EXPAND_EACH_FIDL_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
}};

constexpr std::array<CaptureUsage, kStreamCaptureUsageCount> kCaptureUsages = {{
#define EXPAND_CAPTURE_USAGE(U) CaptureUsage::U,
    EXPAND_EACH_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
}};

static constexpr uint32_t kStreamUsageCount = kStreamRenderUsageCount + kStreamCaptureUsageCount;

// Since we define the RenderUsage enum to have the same numeric values for each fidl enum entry,
// we can convert by casting the underlying numeric value.
static RenderUsage RenderUsageFromFidlRenderUsage(fuchsia::media::AudioRenderUsage u) {
  return static_cast<RenderUsage>(fidl::ToUnderlying(u));
}
static CaptureUsage CaptureUsageFromFidlCaptureUsage(fuchsia::media::AudioCaptureUsage u) {
  return static_cast<CaptureUsage>(fidl::ToUnderlying(u));
}

std::optional<fuchsia::media::AudioRenderUsage> FidlRenderUsageFromRenderUsage(RenderUsage u);

std::optional<fuchsia::media::AudioCaptureUsage> FidlCaptureUsageFromCaptureUsage(CaptureUsage u);

const char* RenderUsageToString(const RenderUsage& usage);
const char* CaptureUsageToString(const CaptureUsage& usage);

class StreamUsage {
 public:
  static constexpr StreamUsage WithRenderUsage(RenderUsage u) { return StreamUsage(u); }
  static constexpr StreamUsage WithCaptureUsage(CaptureUsage u) { return StreamUsage(u); }
  static constexpr StreamUsage WithRenderUsage(fuchsia::media::AudioRenderUsage u) {
    return StreamUsage(RenderUsageFromFidlRenderUsage(u));
  }
  static constexpr StreamUsage WithCaptureUsage(fuchsia::media::AudioCaptureUsage u) {
    return StreamUsage(CaptureUsageFromFidlCaptureUsage(u));
  }

  constexpr StreamUsage() = default;

  constexpr bool operator==(const StreamUsage& other) const { return usage_ == other.usage_; }
  constexpr bool operator!=(const StreamUsage& other) const { return !(*this == other); }

  // RenderUsage
  constexpr bool is_render_usage() const { return std::holds_alternative<RenderUsage>(usage_); }
  constexpr RenderUsage render_usage() const {
    FX_DCHECK(is_render_usage());
    return std::get<RenderUsage>(usage_);
  }

  // CaptureUsage
  constexpr bool is_capture_usage() const { return std::holds_alternative<CaptureUsage>(usage_); }
  constexpr CaptureUsage capture_usage() const {
    FX_DCHECK(is_capture_usage());
    return std::get<CaptureUsage>(usage_);
  }

  // A |StreamUsage| is empty if it contains neither a render usage or a capture usage. This state
  // exists to be similar to the semantics of a FIDL union in C++.
  constexpr bool is_empty() const { return std::holds_alternative<std::monostate>(usage_); }

  const char* ToString() const;

 private:
  using Usage = std::variant<std::monostate, RenderUsage, CaptureUsage>;

  explicit constexpr StreamUsage(RenderUsage usage) : usage_(usage) {}
  explicit constexpr StreamUsage(CaptureUsage usage) : usage_(usage) {}

  Usage usage_;
};

constexpr std::array<StreamUsage, kStreamUsageCount> kStreamUsages = {{
#define EXPAND_RENDER_USAGE(U) StreamUsage::WithRenderUsage(RenderUsage::U),
    EXPAND_EACH_RENDER_USAGE
#undef EXPAND_RENDER_USAGE
#define EXPAND_CAPTURE_USAGE(U) StreamUsage::WithCaptureUsage(CaptureUsage::U),
        EXPAND_EACH_CAPTURE_USAGE
#undef EXPAND_CAPTURE_USAGE
}};

// Guaranteed to be dense, with values ranging from 0 to kStreamUsageCount inclusive.
static constexpr uint32_t HashStreamUsage(const StreamUsage& u) {
  if (u.is_render_usage()) {
    return static_cast<uint32_t>(u.render_usage());
  }
  if (u.is_capture_usage()) {
    return static_cast<uint32_t>(u.capture_usage()) + kStreamRenderUsageCount;
  }
  return kStreamUsageCount;
}

namespace internal {

struct EnumHash {
  template <typename T>
  size_t operator()(T t) const {
    return static_cast<size_t>(t);
  }
};

struct StreamUsageHash {
  size_t operator()(StreamUsage u) const { return HashStreamUsage(u); }
};

}  // namespace internal

using RenderUsageSet = std::unordered_set<RenderUsage, internal::EnumHash>;
using CaptureUsageSet = std::unordered_set<CaptureUsage, internal::EnumHash>;
using StreamUsageSet = std::unordered_set<StreamUsage, internal::StreamUsageHash>;

StreamUsage StreamUsageFromFidlUsage(const fuchsia::media::Usage& usage);

template <typename Container>
static StreamUsageSet StreamUsageSetFromRenderUsages(const Container& container) {
  static_assert(std::is_same<typename Container::value_type, RenderUsage>::value);
  StreamUsageSet result;
  std::transform(container.cbegin(), container.cend(), std::inserter(result, result.begin()),
                 [](const auto& u) { return StreamUsage::WithRenderUsage(u); });
  return result;
}

template <typename Container>
static StreamUsageSet StreamUsageSetFromCaptureUsages(const Container& container) {
  static_assert(std::is_same<typename Container::value_type, CaptureUsage>::value);
  StreamUsageSet result;
  std::transform(container.cbegin(), container.cend(), std::inserter(result, result.begin()),
                 [](const auto& u) { return StreamUsage::WithCaptureUsage(u); });
  return result;
}

// A set of StreamUsages represented as a bitmask.
class StreamUsageMask final {
 public:
  constexpr StreamUsageMask() = default;
  constexpr StreamUsageMask(const StreamUsageMask& other) = default;
  constexpr StreamUsageMask(std::initializer_list<StreamUsage> usages) {
    for (const auto& usage : usages) {
      insert(usage);
    }
  }

  constexpr StreamUsageMask& operator=(const StreamUsageMask& other) = default;

  static constexpr StreamUsageMask FromMask(uint32_t mask) {
    StreamUsageMask s;
    s.mask_ = mask;
    return s;
  }

  // Insert `usage` into the bitmask.
  constexpr void insert(const StreamUsage& usage) {
    if (!usage.is_empty()) {
      mask_ |= (1 << HashStreamUsage(usage));
    }
  }

  // Insert all of the StreamUsages from `other`.
  constexpr void insert_all(const StreamUsageMask& other) { mask_ |= other.mask_; }

  // Unsets `usage` from the bitmask.
  constexpr void erase(const StreamUsage& usage) {
    if (!usage.is_empty()) {
      mask_ &= ~(1 << HashStreamUsage(usage));
    }
  }

  // Returns true iff there are no usages in the mask.
  constexpr bool is_empty() const { return mask_ == 0; }

  // Clears all elements from the bitmask.
  constexpr void clear() { mask_ = 0; }

  // Returns true iff `usage` is set.
  constexpr bool contains(const StreamUsage& usage) const {
    return !usage.is_empty() && mask_ & (1 << HashStreamUsage(usage));
  }

  // Returns the raw bitmask.
  constexpr uint32_t mask() const { return mask_; }

  constexpr bool operator==(const StreamUsageMask& other) const { return mask_ == other.mask_; }
  constexpr bool operator!=(const StreamUsageMask& other) const { return !(*this == other); }

 private:
  uint32_t mask_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_STREAM_USAGE_H_
