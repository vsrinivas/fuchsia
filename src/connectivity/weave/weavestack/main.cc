// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/weavestack/app.h"

int main(void) {
  weavestack::App app;
  zx_status_t status;

  status = app.Init();
  if (status != ZX_OK) {
    return status;
  }

  status = app.Run();
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}
