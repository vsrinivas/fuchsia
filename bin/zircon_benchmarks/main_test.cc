// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_runner.h"

int main(int argc, char** argv) {
  return fbenchmark::BenchmarksMain(argc, argv, /* run_gbenchmark= */ false);
}
