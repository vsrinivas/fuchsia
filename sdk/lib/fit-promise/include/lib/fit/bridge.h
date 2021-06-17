// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(78633): Remove stub header when users are ported to the fpromise namespace and include path.

#ifndef LIB_FIT_PROMISE_INCLUDE_LIB_FIT_BRIDGE_H_
#define LIB_FIT_PROMISE_INCLUDE_LIB_FIT_BRIDGE_H_

#include <lib/fpromise/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>

namespace fit {

using fpromise::bridge;
using fpromise::completer;
using fpromise::consumer;
using fpromise::schedule_for_consumer;

}  // namespace fit

#endif  // LIB_FIT_PROMISE_INCLUDE_LIB_FIT_BRIDGE_H_
