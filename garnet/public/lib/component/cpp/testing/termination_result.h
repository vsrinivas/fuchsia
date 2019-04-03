// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! DEPRECATED !!!
// New usages should reference sdk/lib/sys/cpp/...

#ifndef LIB_COMPONENT_CPP_TESTING_TERMINATION_RESULT_H_
#define LIB_COMPONENT_CPP_TESTING_TERMINATION_RESULT_H_

namespace component {
namespace testing {

// Combines the return code and termination reason from a Component termination.
struct TerminationResult {
  int64_t return_code;
  fuchsia::sys::TerminationReason reason;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_COMPONENT_CPP_TESTING_TERMINATION_RESULT_H_