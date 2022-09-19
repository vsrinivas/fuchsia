// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_helpers.h"

std::ostream& operator<<(std::ostream& os, const fuchsia::bluetooth::PeerId& peer_id) {
  return os << fxl::StringPrintf("%.16lx", peer_id.value);
}

std::ostream& operator<<(std::ostream& os, const fuchsia::bluetooth::gatt2::Handle& handle) {
  return os << handle.value;
}
