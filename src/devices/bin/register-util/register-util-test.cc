// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "register-util.h"

#include <errno.h>
#include <fuchsia/hardware/registers/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

class PhyServer : public llcpp::fuchsia::hardware::registers::Device::Interface {
 public:
  PhyServer() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    zx::channel channels[2];
    zx::channel::create(0, channels, channels + 1);
    channel_ = std::move(channels[1]);
    fidl::BindServer(loop_.dispatcher(), std::move(channels[0]), this);
    loop_.StartThread();
  }
  void WriteRegister(uint64_t address, uint32_t value,
                     WriteRegisterCompleter::Sync completer) override {
    address_ = address;
    value_ = value;
    completer.ReplySuccess();
  }
  zx::channel TakeChannel() { return std::move(channel_); }
  uint64_t address() { return address_; }

  uint64_t value() { return value_; }

 private:
  uint64_t address_ = 0;
  uint64_t value_ = 0;
  async::Loop loop_;
  zx::channel channel_;
};

TEST(RegisterUtil, RegisterWriteTest) {
  const char* args[] = {"", "/bin/register-util", "50", "90"};
  PhyServer server;
  ASSERT_EQ(run(3, args, server.TakeChannel()), 0);
  ASSERT_EQ(server.address(), 0x50);
  ASSERT_EQ(server.value(), 0x90);
}
