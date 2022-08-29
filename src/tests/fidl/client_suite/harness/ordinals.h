// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_ORDINALS_H_
#define SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_ORDINALS_H_

#include <cstdint>

namespace client_suite {

// To find all ordinals:
//
//     cat
//     out/default/fidling/gen/src/tests/fidl/client_suite/fidl/fidl.clientsuite/llcpp/fidl/fidl.clientsuite/cpp/wire_messaging.cc
//     | grep -e 'constexpr.*kTarget.*Ordinal' -A 1
//
// While using `jq` would be much nicer, large numbers are mishandled and the
// displayed ordinal ends up being incorrect.
//
// Ordinals are redefined here even though they may be accessible via C++
// binding definitions to ensure they are unchanged by changes in the bindings.
static const uint64_t kOrdinalTwoWayNoPayload = 8823160117673072416lu;

}  // namespace client_suite

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_ORDINALS_H_
