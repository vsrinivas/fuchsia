// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lib/fxl/logging.h>

#include "test_runner.h"

namespace {

// Measure the time taken by stat() on the current directory.
void StatTest() {
  struct stat st;
  FXL_CHECK(stat(".", &st) == 0);
}

// Measure the time taken by open()+close() on the current directory.
void OpenTest() {
  int fd = open(".", O_RDONLY);
  FXL_CHECK(fd >= 0);
  FXL_CHECK(close(fd) == 0);
}

// Measure the time taken by fstat() on an FD for the current directory.
class FstatTest {
 public:
  FstatTest() {
    fd_ = open(".", O_RDONLY);
    FXL_CHECK(fd_ >= 0);
  }

  ~FstatTest() { FXL_CHECK(close(fd_) == 0); }

  void Run() {
    struct stat st;
    FXL_CHECK(fstat(fd_, &st) == 0);
  }

 private:
  int fd_;
};

__attribute__((constructor)) void RegisterTests() {
  fbenchmark::RegisterTestFunc<StatTest>("Filesystem_Stat");
  fbenchmark::RegisterTestFunc<OpenTest>("Filesystem_Open");
  fbenchmark::RegisterTest<FstatTest>("Filesystem_Fstat");
}

}  // namespace
