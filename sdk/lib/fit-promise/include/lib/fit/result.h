// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(78633): Remove stub header when users are ported to the fpromise namespace and include path.

#ifndef LIB_FIT_PROMISE_INCLUDE_LIB_FIT_RESULT_H_
#define LIB_FIT_PROMISE_INCLUDE_LIB_FIT_RESULT_H_

#include <lib/fpromise/result.h>

namespace fit {

using fpromise::pending_result;
using fpromise::pending;
using fpromise::ok_result;
using fpromise::ok;
using fpromise::error_result;
using fpromise::error;
using fpromise::result_state;
using fpromise::result;
using fpromise::swap;

using fpromise::in_place_t;
using fpromise::in_place_type_t;
using fpromise::in_place_index_t;
using fpromise::in_place;
using fpromise::in_place_type;
using fpromise::in_place_index;

}  // namespace fit

#endif  // LIB_FIT_PROMISE_INCLUDE_LIB_FIT_RESULT_H_
