// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reliability_and_ordering.h"

namespace overnet {

const char* ReliabilityAndOrderingString(
    ReliabilityAndOrdering reliability_and_ordering) {
  switch (reliability_and_ordering) {
    case ReliabilityAndOrdering::ReliableOrdered:
      return "ReliableOrdered";
    case ReliabilityAndOrdering::UnreliableOrdered:
      return "UnreliableOrdered";
    case ReliabilityAndOrdering::ReliableUnordered:
      return "ReliableUnordered";
    case ReliabilityAndOrdering::UnreliableUnordered:
      return "UnreliableUnordered";
    case ReliabilityAndOrdering::TailReliable:
      return "TailReliable";
  }
  return "UnknownReliabilityAndOrdering";
}

}  // namespace overnet
