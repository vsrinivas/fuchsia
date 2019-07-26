// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/pager.h>

namespace zx {

zx_status_t pager::create(uint32_t options, pager* result) {
  return zx_pager_create(options, result->reset_and_get_address());
}

}  // namespace zx
