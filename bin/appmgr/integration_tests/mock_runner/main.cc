// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/mock_runner/mock_runner.h"

int main(int argc, char** argv) {
  component::testing::MockRunner mock_runner;
  mock_runner.Start();
  return 0;
}
