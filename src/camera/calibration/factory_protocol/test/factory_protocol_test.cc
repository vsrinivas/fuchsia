// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>

#include "src/lib/files/directory.h"

namespace camera {
namespace {

constexpr const auto kDirPath = "/data/calibration";
constexpr const auto kFilename = "/frame_0.raw";
constexpr uint8_t kStrLength = 17;

TEST(FactoryProtocolTest, DISABLED_ConstructorSanity) {
  thrd_t thread;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("test-thread", &thread));

  zx::channel channel;
  auto factory_impl = FactoryProtocol::Create(std::move(channel), loop.dispatcher());
  ASSERT_NE(nullptr, factory_impl);
}

TEST(FactoryProtocolTest, DISABLED_StreamingWritesToFile) {
  thrd_t thread;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("test-thread", &thread));

  zx::channel channel;
  auto factory_impl = FactoryProtocol::Create(std::move(channel), loop.dispatcher());
  ASSERT_NE(nullptr, factory_impl);
  ASSERT_EQ(ZX_OK, factory_impl->ConnectToStream());
  const std::string kDirPathStr(kDirPath, kStrLength);
  ASSERT_TRUE(files::IsDirectory(kDirPathStr));
  ASSERT_FALSE(factory_impl->frames_received());

  while (!factory_impl->frames_received()) {
    loop.RunUntilIdle();
  }

  auto path = kDirPathStr + kFilename;
  ASSERT_TRUE(files::IsFile(path));
}

TEST(FactoryProtocolTest, DISABLED_ShutdownClosesChannelAndStream) {
  thrd_t thread;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("test-thread", &thread));

  zx::channel channel;
  auto factory_impl = FactoryProtocol::Create(std::move(channel), loop.dispatcher());
  ASSERT_NE(nullptr, factory_impl);
  ASSERT_FALSE(factory_impl->streaming());
  ASSERT_EQ(ZX_OK, factory_impl->ConnectToStream());
  loop.RunUntilIdle();
  ASSERT_TRUE(factory_impl->streaming());

  factory_impl->Shutdown(ZX_ERR_STOP);
  loop.RunUntilIdle();

  zx_status_t status = channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    EXPECT_EQ(status, ZX_ERR_BAD_HANDLE);
  }

  ASSERT_FALSE(factory_impl->streaming());
}

}  // namespace
}  // namespace camera
