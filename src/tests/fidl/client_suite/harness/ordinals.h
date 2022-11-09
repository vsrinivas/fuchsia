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
//     out/default/fidling/gen/src/tests/fidl/client_suite/fidl/fidl.clientsuite/cpp/fidl/fidl.clientsuite/cpp/wire_messaging.cc
//     | grep -e 'constexpr.*k.*Target.*Ordinal' -A 1
//
// While using `jq` would be much nicer, large numbers are mishandled and the
// displayed ordinal ends up being incorrect.
//
// Ordinals are redefined here even though they may be accessible via C++
// binding definitions to ensure they are unchanged by changes in the bindings.

// Closed Target Ordinals
static const uint64_t kOrdinalTwoWayNoPayload = 8823160117673072416lu;
static const uint64_t kOrdinalTwoWayStructPayload = 4197391242806919613lu;

// Open Target Ordinals
static const uint64_t kOrdinalStrictOneWay = 7904024199254697828lu;
static const uint64_t kOrdinalFlexibleOneWay = 1119507890521778684lu;
static const uint64_t kOrdinalStrictTwoWay = 995098632483095874lu;
static const uint64_t kOrdinalStrictTwoWayFields = 8775166568163566307lu;
static const uint64_t kOrdinalStrictTwoWayErr = 6063427118545698440lu;
static const uint64_t kOrdinalStrictTwoWayFieldsErr = 9059738677182539537lu;
static const uint64_t kOrdinalFlexibleTwoWay = 7373850992214410631lu;
static const uint64_t kOrdinalFlexibleTwoWayFields = 4087393006943873764lu;
static const uint64_t kOrdinalFlexibleTwoWayErr = 7932048879253591162lu;
static const uint64_t kOrdinalFlexibleTwoWayFieldsErr = 1029425766479692362lu;
static const uint64_t kOrdinalStrictEvent = 3110049402570307488lu;
static const uint64_t kOrdinalFlexibleEvent = 5824395118547404952lu;

// Common Ordinals
// A made-up ordinal used when a method is needed that isn't known to the
// server.
static const uint64_t kOrdinalFakeUnknownMethod = 0x10ff10ff10ff10fflu;
}  // namespace client_suite

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_ORDINALS_H_
