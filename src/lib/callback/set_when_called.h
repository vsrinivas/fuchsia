// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CALLBACK_SET_WHEN_CALLED_H_
#define SRC_LIB_CALLBACK_SET_WHEN_CALLED_H_

#include <lib/fit/function.h>

namespace callback {

// Returns a function that sets a boolean to true when called. For checking
// whether an asynchronous call results in the expected callback.
//
// When this function is called, it initially sets the boolean value to false.
fit::closure SetWhenCalled(bool* value);

}  // namespace callback

#endif  // SRC_LIB_CALLBACK_SET_WHEN_CALLED_H_
