// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_UTIL_H_

#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>
#include <lib/stdcompat/source_location.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <algorithm>
#include <string>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/lib/numerics/rational.h"

namespace camera {

using LoadFirmwareCallback =
    fit::function<fpromise::result<std::pair<zx::vmo, size_t>, zx_status_t>(const std::string&)>;

inline fit::function<void(zx_status_t)> MakeErrorHandler(
    bool panic = true, cpp20::source_location location = cpp20::source_location::current()) {
  std::string origin;
  if (location.file_name()) {
    origin += location.file_name();
  } else {
    origin += "<unknown>";
  }
  origin += ":" + std::to_string(location.line());
  if (location.function_name()) {
    origin += " (" + std::string(location.file_name()) + ")";
  }
  return [origin, panic](zx_status_t status) {
    auto message = "Unexpected error from " + origin;
    FX_PLOGS(ERROR, status) << message;
    if (panic) {
      ZX_PANIC("%s: %s", message.c_str(), zx_status_get_string(status));
    }
  };
}

inline constexpr const char* NodeTypeName(const InternalConfigNode& node) {
  switch (node.type) {
    case NodeType::kInputStream:
      return "input";
    case NodeType::kGdc:
      return "gdc";
    case NodeType::kGe2d:
      return "ge2d";
    case NodeType::kOutputStream:
      return "output";
    case NodeType::kPassthrough:
      return "passthrough";
    default:
      return "<invalid>";
  }
}

// This function helps chain together async methods to provide continuation semantics. While inline
// lambdas can easily transform `foo(callback)` into `bar([&]{foo(callback);})`, this function can
// transform it into a form equivalent to `foo([&]{bar(callback);})`
inline fit::function<void(fit::closure)> MakeContinuation(fit::function<void(fit::closure)> func,
                                                          fit::function<void(fit::closure)> then) {
  return [func = std::move(func), then = std::move(then)](fit::closure callback) mutable {
    func([then = std::move(then), callback = std::move(callback)]() mutable {
      then(std::move(callback));
    });
  };
}

// Converts a camera2.FrameRate (Hz) to a frame time interval (seconds).
inline numerics::Rational FramerateToInterval(const fuchsia::camera2::FrameRate& fps) {
  ZX_ASSERT(fps.frames_per_sec_numerator > 0);
  ZX_ASSERT(fps.frames_per_sec_denominator > 0);
  // Note: the swap of numerator and denominator fields is intentional.
  numerics::Rational interval{.n = fps.frames_per_sec_denominator,
                              .d = fps.frames_per_sec_numerator};
  interval.Reduce();
  return interval;
}

// Syntactic sugar for vector move-append.
template <typename T>
static std::vector<T>& operator+=(std::vector<T>& a, std::vector<T>&& b) {
  assert(&a != &b);
  a.reserve(a.size() + b.size());
  std::move(b.begin(), b.end(), std::back_inserter(a));
  return a;
}

// This method extends fit::bind_member with an implicit derived-class cast.
template <typename R, typename T, typename U = T, typename... Args>
auto BindPolyMethod(T* instance, R (U::*fn)(Args...)) {
  static_assert(std::is_base_of_v<T, U>, "BindPolyMethod requires compatible types.");
  return fit::bind_member(static_cast<U*>(instance), fn);
}

// Formats a vector into a string using the given separators.
template <typename T>
std::string Format(const std::vector<T>& v,
                   const std::array<char, 3> separators = {'[', ']', ':'}) {
  std::string formatted;
  formatted += separators[0];
  for (const auto& e : v) {
    formatted += std::to_string(e) + separators[2];
  }
  formatted.pop_back();
  formatted += separators[1];
  return formatted;
}

// Invokes the provided function for each line (spans between newline characters) of the provided
// string. This is used for breaking up long syslog messages to stay under the message size limit.
inline void ForEachLine(const std::string& s, fit::function<void(const std::string&)> fn) {
  auto begin = s.begin();
  while (begin != s.end()) {
    auto it = std::find(begin, s.end(), '\n');
    std::string str(begin, it);
    fn(str);
    begin = it;
  }
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_UTIL_H_
