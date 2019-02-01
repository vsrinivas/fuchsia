// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_INSPECT_UNITTESTS_MATCHERS_H_
#define GARNET_BIN_INSPECT_UNITTESTS_MATCHERS_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include "gmock/gmock.h"

MATCHER_P2(StringProperty, name, value, "") {
  return arg.value.is_str() && arg.key == name && arg.value.str() == value;
}

MATCHER_P2(ByteVectorProperty, name, value, "") {
  return arg.value.is_bytes() && arg.key == name && arg.value.bytes() == value;
}

MATCHER_P2(UIntMetric, name, value, "") {
  return arg.key == name && arg.value.is_uint_value() &&
         arg.value.uint_value() == static_cast<uint64_t>(value);
}

MATCHER_P2(IntMetric, name, value, "") {
  return arg.key == name && arg.value.is_int_value() &&
         arg.value.int_value() == static_cast<int64_t>(value);
}

MATCHER_P2(DoubleMetric, name, value, "") {
  return arg.key == name && arg.value.is_double_value() &&
         arg.value.double_value() == static_cast<double>(value);
}

#endif  // GARNET_BIN_INSPECT_UNITTESTS_MATCHERS_H_
