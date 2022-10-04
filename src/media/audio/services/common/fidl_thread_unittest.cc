// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/common/fidl_thread.h"

#include <lib/sync/cpp/completion.h>

#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(FidlThreadTest, CreateFromNewThread) {
  auto thread = FidlThread::CreateFromNewThread("test_fidl_thread");
  EXPECT_FALSE(thread->checker().IsValid());

  libsync::Completion done;
  async::PostTask(thread->dispatcher(), [&done, thread]() {
    EXPECT_TRUE(thread->checker().IsValid());
    done.Signal();
  });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(FidlThreadTest, CreateFromCurrentThread) {
  const async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  auto thread = FidlThread::CreateFromCurrentThread("test_fidl_thread", loop.dispatcher());
  EXPECT_TRUE(thread->checker().IsValid());

  libsync::Completion done;
  async::PostTask(thread->dispatcher(), [&done, thread]() {
    EXPECT_TRUE(thread->checker().IsValid());
    done.Signal();
  });

  thread->loop().RunUntilIdle();
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

}  // namespace
}  // namespace media_audio
