// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <perftest/perftest.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"

namespace {

// Measure the time taken by stat() on the current directory.
bool StatTest() {
  struct stat st;
  FXL_CHECK(stat(".", &st) == 0);
  return true;
}

// Measure the time taken by open()+close() on the current directory.
bool OpenTest() {
  int fd = open(".", O_RDONLY);
  FXL_CHECK(fd >= 0);
  FXL_CHECK(close(fd) == 0);
  return true;
}

// Measure the time taken by fstat() on an FD for the current directory.
bool FstatTest(perftest::RepeatState* state) {
  fbl::unique_fd fd(open(".", O_RDONLY));
  FXL_CHECK(fd.is_valid());

  while (state->KeepRunning()) {
    struct stat st;
    FXL_CHECK(fstat(fd.get(), &st) == 0);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterSimpleTest<StatTest>("Filesystem_Stat");
  perftest::RegisterSimpleTest<OpenTest>("Filesystem_Open");
  perftest::RegisterTest("Filesystem_Fstat", FstatTest);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
