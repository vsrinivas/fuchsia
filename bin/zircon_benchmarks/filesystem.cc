// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <benchmark/benchmark.h>

namespace {

// Measure the time taken by stat() on the current directory.
void Filesystem_Stat(benchmark::State& state) {
  while (state.KeepRunning()) {
    struct stat st;
    if (stat(".", &st) != 0) {
      state.SkipWithError("stat() failed");
      return;
    }
  }
}
BENCHMARK(Filesystem_Stat);

// Measure the time taken by open()+close() on the current directory.
void Filesystem_Open(benchmark::State& state) {
  while (state.KeepRunning()) {
    int fd = open(".", O_RDONLY);
    if (fd < 0) {
      state.SkipWithError("open() failed");
      return;
    }
    if (close(fd) != 0) {
      state.SkipWithError("close() failed");
      return;
    }
  }
}
BENCHMARK(Filesystem_Open);

// Measure the time taken by fstat() on an FD for the current directory.
void Filesystem_Fstat(benchmark::State& state) {
  int fd = open(".", O_RDONLY);
  if (fd < 0) {
    state.SkipWithError("open() failed");
    return;
  }

  while (state.KeepRunning()) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
      state.SkipWithError("fstat() failed");
      break;
    }
  }

  // Clean up.
  if (close(fd) != 0) {
    state.SkipWithError("close() failed");
    return;
  }
}
BENCHMARK(Filesystem_Fstat);

}  // namespace
