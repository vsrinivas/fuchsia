// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handle_util.h"

#include <zircon/assert.h>

namespace fidl {
namespace test {
namespace util {

zx_handle_t create_channel() {
  zx_handle_t c1, c2;
  ZX_ASSERT(zx_channel_create(0, &c1, &c2) == ZX_OK);
  zx_handle_close(c1);
  return c2;
}

zx_handle_t create_event() {
  zx_handle_t e;
  ZX_ASSERT(zx_event_create(0, &e) == ZX_OK);
  return e;
}

}  // namespace util
}  // namespace test
}  // namespace fidl
