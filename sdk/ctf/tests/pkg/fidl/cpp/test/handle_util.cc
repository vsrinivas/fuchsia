// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handle_util.h"

#include <zircon/assert.h>

namespace fidl {
namespace test {
namespace util {

zx_handle_t HandleReplace(zx_handle_t handle, zx_rights_t rights) {
  zx_handle_t replaced_handle;
  ZX_ASSERT(zx_handle_replace(handle, rights, &replaced_handle) == ZX_OK);
  return replaced_handle;
}

zx_handle_t CreateChannel(zx_rights_t rights) {
  zx_handle_t c1, c2;
  ZX_ASSERT(zx_channel_create(0, &c1, &c2) == ZX_OK);
  zx_handle_close(c1);
  return HandleReplace(c2, rights);
}

zx_handle_t CreateEvent(zx_rights_t rights) {
  zx_handle_t e;
  ZX_ASSERT(zx_event_create(0, &e) == ZX_OK);
  return HandleReplace(e, rights);
}

}  // namespace util
}  // namespace test
}  // namespace fidl
