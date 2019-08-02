// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_TYPES_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_TYPES_H_

#include <string_view>

namespace broker_service::cobalt {
// Supported metric mappings from Inspect to Cobalt.
enum class SupportedType {
  // Unknown type.
  kUnknown,
  // This includes any inspect histogram type.
  kHistogram,
  // This includes any counter.
  kCounter,
};

// Returns the corresponding |SupportedType| for a string value.
// Conversion is as follows:
//    SupportedType::kType == GetSupportedType("TYPE")
//    Any other case SupportedType::kUnknown.
SupportedType GetSupportedType(std::string_view value);

}  // namespace broker_service::cobalt

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_TYPES_H_
