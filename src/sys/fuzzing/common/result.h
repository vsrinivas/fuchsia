// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RESULT_H_
#define SRC_SYS_FUZZING_COMMON_RESULT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

namespace fuzzing {

// This file simply aliases the FIDL |Result|. There aren't corresponding functions or types as with
// |Input|, |Options|, or |Status|. Still, it is useful to have a dedicated header to distinguish
// this type from other results, e.g. |fpromise::result<V, E>|.
using FuzzResult = ::fuchsia::fuzzer::Result;

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RESULT_H_
