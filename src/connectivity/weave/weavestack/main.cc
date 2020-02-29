// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/weavestack/app.h"

int main(void) {
  weavestack::App app;

  WEAVE_ERROR ret = app.Init();
  if (ret != WEAVE_NO_ERROR) {
    return ret;
  }

  ret = app.Start();
  if (ret != WEAVE_NO_ERROR) {
    return ret;
  }

  // Wait for all the threads to complete.
  app.Join();

  return WEAVE_NO_ERROR;
}
