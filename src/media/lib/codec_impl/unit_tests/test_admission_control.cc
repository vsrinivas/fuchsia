// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>
#include <lib/zx/port.h>

#include <array>

#include <gtest/gtest.h>

#include "lib/media/codec_impl/codec_admission_control.h"

TEST(AdmissionControl, DelayedAdmission) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  CodecAdmissionControl control(loop.dispatcher());

  std::unique_ptr<CodecAdmission> admission;
  control.TryAddCodec(false, [&admission](std::unique_ptr<CodecAdmission> new_admission) {
    admission = std::move(new_admission);
  });

  EXPECT_FALSE(admission);
  loop.RunUntilIdle();
  EXPECT_TRUE(admission);

  bool got_callback = false;
  control.TryAddCodec(false, [&got_callback](std::unique_ptr<CodecAdmission> new_admission) {
    EXPECT_FALSE(new_admission);
    got_callback = true;
  });
  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_TRUE(got_callback);

  admission->SetCodecIsClosing();
  got_callback = false;
  control.TryAddCodec(false, [&got_callback](std::unique_ptr<CodecAdmission> new_admission) {
    EXPECT_TRUE(new_admission);
    got_callback = true;
  });

  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_FALSE(got_callback);
  admission = nullptr;
  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_TRUE(got_callback);
}

TEST(AdmissionControl, DelayedMultiAdmission) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  CodecAdmissionControl control(loop.dispatcher());

  std::array<std::unique_ptr<CodecAdmission>, 2> admission;
  control.TryAddCodec(true, [&admission](std::unique_ptr<CodecAdmission> new_admission) {
    admission[0] = std::move(new_admission);
  });
  control.TryAddCodec(true, [&admission](std::unique_ptr<CodecAdmission> new_admission) {
    admission[1] = std::move(new_admission);
  });

  EXPECT_FALSE(admission[0]);
  EXPECT_FALSE(admission[1]);
  loop.RunUntilIdle();
  EXPECT_TRUE(admission[0]);
  EXPECT_TRUE(admission[1]);

  admission[0]->SetCodecIsClosing();
  admission[1]->SetCodecIsClosing();

  // This should wait for the existing codec closes to run before executing.
  bool got_callback = false;
  control.TryAddCodec(false, [&got_callback](std::unique_ptr<CodecAdmission> new_admission) {
    EXPECT_TRUE(new_admission);
    got_callback = true;
  });

  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_FALSE(got_callback);

  admission[0] = nullptr;
  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_FALSE(got_callback);

  admission[1] = nullptr;
  loop.RunUntilIdle();
  EXPECT_TRUE(got_callback);
}

TEST(AdmissionControl, ChannelClose) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  CodecAdmissionControl control(loop.dispatcher());

  std::unique_ptr<CodecAdmission> admission;
  control.TryAddCodec(false, [&admission](std::unique_ptr<CodecAdmission> new_admission) {
    admission = std::move(new_admission);
  });

  loop.RunUntilIdle();
  EXPECT_TRUE(admission);

  zx::channel server_end, client_end;
  ASSERT_EQ(ZX_OK, zx::channel::create(0u, &server_end, &client_end));
  admission->SetChannelToWaitOn(client_end);

  bool got_callback = false;
  control.TryAddCodec(false, [&got_callback](std::unique_ptr<CodecAdmission> new_admission) {
    EXPECT_FALSE(new_admission);
    got_callback = true;
  });

  // Server end is open, so this should fail.
  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_TRUE(got_callback);

  server_end.reset();
  // Server end closing should be detected before client end closing cancels the wait.
  client_end.reset();

  // Server end is closed, so this should wait for the existing admission to exit.
  got_callback = false;
  control.TryAddCodec(false, [&got_callback](std::unique_ptr<CodecAdmission> new_admission) {
    EXPECT_TRUE(new_admission);
    got_callback = true;
  });
  EXPECT_FALSE(got_callback);
  loop.RunUntilIdle();
  EXPECT_FALSE(got_callback);

  admission = nullptr;
  loop.RunUntilIdle();
  EXPECT_TRUE(got_callback);
}
