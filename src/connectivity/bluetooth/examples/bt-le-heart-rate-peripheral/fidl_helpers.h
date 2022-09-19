// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_FIDL_HELPERS_
#define SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_FIDL_HELPERS_

#include <fuchsia/bluetooth/cpp/fidl.h>
#include <fuchsia/bluetooth/gatt2/cpp/fidl.h>

#include <iostream>

#include <src/lib/fxl/strings/string_printf.h>

std::ostream& operator<<(std::ostream& os, const fuchsia::bluetooth::PeerId& peer_id);

std::ostream& operator<<(std::ostream& os, const fuchsia::bluetooth::gatt2::Handle& handle);

#endif /* SRC_CONNECTIVITY_BLUETOOTH_EXAMPLES_BT_LE_HEART_RATE_PERIPHERAL_FIDL_HELPERS_ */
