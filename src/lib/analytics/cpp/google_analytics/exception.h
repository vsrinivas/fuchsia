// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_EXCEPTION_H_
#define SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_EXCEPTION_H_

#include <optional>
#include <string_view>

#include "src/lib/analytics/cpp/google_analytics/hit.h"

namespace analytics::google_analytics {

// Representation of a Google Analytics exception hit.
// See
// https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#exception
class Exception : public Hit {
 public:
  explicit Exception(std::optional<std::string_view> description = std::nullopt,
                     std::optional<bool> is_fatal = std::nullopt);
};

}  // namespace analytics::google_analytics

#endif  // SRC_LIB_ANALYTICS_CPP_GOOGLE_ANALYTICS_EXCEPTION_H_
