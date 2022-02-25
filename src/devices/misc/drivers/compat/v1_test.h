// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>
#include <zircon/types.h>

struct V1Test {
  zx_status_t status = ZX_OK;
  bool did_bind = false;
  bool did_create = false;
  bool did_release = false;
  zx_device_t* zxdev = nullptr;
};
