// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>

#include "channels.h"

DEFINE_uint32(channel_read, 0, "Launch a process to read from a channel");
DEFINE_uint32(channel_write, 0, "Launch a process to write to a channel");

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_channel_read > 0) {
    return channel_read(FLAGS_channel_read);
  } else if (FLAGS_channel_write > 0) {
    return channel_write(FLAGS_channel_write);
  }

  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
