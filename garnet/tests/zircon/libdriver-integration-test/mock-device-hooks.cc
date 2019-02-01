// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-device-hooks.h"

namespace libdriver_integration_test {

MockDeviceHooks::MockDeviceHooks(Completer completer)
    : completer_(std::move(completer)) {
}

} // namespace libdriver_integration_test
