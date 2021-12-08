// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/channel.h>

#include <perftest/perftest.h>

namespace {

// These tests measure the times taken to create and close various types of
// fdf handles. Strictly speaking, they test creating fdf objects as
// well as creating handles.
//
// In each test, closing the handles is done implicitly by destructors.

bool ChannelCreateTest(perftest::RepeatState* state) {
  state->DeclareStep("create");
  state->DeclareStep("close");
  while (state->KeepRunning()) {
    auto channels = fdf::ChannelPair::Create(0);
    ZX_ASSERT(channels.status_value() == ZX_OK);
    state->NextStep();
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("HandleCreate_Channel", ChannelCreateTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
