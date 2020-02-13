// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_MOCK_DEVICE_FIDL_H_
#define SRC_DEVICES_TESTS_MOCK_DEVICE_FIDL_H_

#include <fuchsia/device/mock/llcpp/fidl.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>

#include <fbl/array.h>
#include <fbl/string_piece.h>

namespace mock_device {

namespace device_mock = ::llcpp::fuchsia::device::mock;

// Returns ZX_ERR_STOP if channel has been closed
// Returns ZX_OK and populates |actions_out| on success.
zx_status_t WaitForPerformActions(const zx::channel& c,
                                  fbl::Array<device_mock::Action>* actions_out);

}  // namespace mock_device

#endif  // SRC_DEVICES_TESTS_MOCK_DEVICE_FIDL_H_
