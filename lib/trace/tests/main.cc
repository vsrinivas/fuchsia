// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "mojo/public/cpp/application/application_test_base.h"

MojoResult MojoMain(MojoHandle handle) {
  return mojo::test::RunAllTests(handle);
}
