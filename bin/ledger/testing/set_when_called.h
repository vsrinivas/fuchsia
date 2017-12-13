// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_SET_WHEN_CALLED_H_
#define PERIDOT_BIN_LEDGER_TESTING_SET_WHEN_CALLED_H_

#include "lib/fxl/functional/closure.h"

namespace ledger {

// Returns a function that sets a boolean to true when called. For checking
// whether an asynchronous call results in the expected callback.
//
// When this function is called, it initially sets the boolean value to false.
fxl::Closure SetWhenCalled(bool* value);

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_SET_WHEN_CALLED_H_
