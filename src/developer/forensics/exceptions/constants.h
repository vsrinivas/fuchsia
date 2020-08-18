// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CONSTANTS_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CONSTANTS_H_

#include <lib/zx/time.h>

namespace forensics {
namespace exceptions {

constexpr size_t kMaxNumExceptionHandlers = 10;
constexpr zx::duration kExceptionTtl = zx::min(5);

// The handler waits on a response from the component lookup service for 30 seconds.
//
// 30 seconds is not the upper bound on how long exception handling takes in total because minidump
// generation happens prior to component lookup and could take some arbitrary time.
constexpr zx::duration kComponentLookupTimeout{zx::sec(30)};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_CONSTANTS_H_
