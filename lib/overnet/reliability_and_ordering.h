// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#pragma once

namespace overnet {

// Reliability and ordering mode for a stream
enum class ReliabilityAndOrdering : uint8_t {
  ReliableOrdered = 0,
  UnreliableOrdered = 1,
  ReliableUnordered = 2,
  UnreliableUnordered = 3,
  // The last sent message in a stream is reliable, and sending a message makes
  // all previous messages in the stream unreliable.
  TailReliable = 4,
};

const char* ReliabilityAndOrderingString(
    ReliabilityAndOrdering reliability_and_ordering);

}  // namespace overnet
