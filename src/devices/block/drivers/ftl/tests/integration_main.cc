// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "ftl_test_observer.h"

int main(int argc, char** argv) {
  FtlTestObserver observer;
  observer.OnProgramStart();
  if (!observer) {
    return -1;
  }

  return RUN_ALL_TESTS(argc, argv);
}
