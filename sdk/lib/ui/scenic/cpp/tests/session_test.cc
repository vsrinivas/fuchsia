// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/ui/scenic/cpp/session.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "lib/ui/scenic/cpp/commands.h"

namespace {

class MockScenicSession : public scenic::Session {
 public:
  MockScenicSession(fuchsia::ui::scenic::SessionPtr client) : Session(std::move(client)) {}

  void Flush() override {
    num_flushed_++;
    commands_.clear();
  };

  int64_t num_flushed_ = 0;
  size_t num_commands() { return commands_.size(); }
  int64_t num_bytes() { return commands_num_bytes_; }
  int64_t num_handles() { return commands_num_handles_; }
};

class ScenicSessionFlushTest : public ::gtest::TestLoopFixture {
 public:
  ScenicSessionFlushTest() = default;
  ~ScenicSessionFlushTest() override = default;

  std::unique_ptr<MockScenicSession> session_;

 protected:
  void SetUp() override {
    zx::channel client_end, server_end;
    zx::channel::create(0, &client_end, &server_end);
    fuchsia::ui::scenic::SessionPtr client;
    client.Bind(std::move(client_end));
    session_ = std::make_unique<MockScenicSession>(std::move(client));
  }

  void TearDown() override { session_.reset(); }
};

TEST_F(ScenicSessionFlushTest, AddOneInputCommand) {
  fuchsia::ui::input::SendPointerInputCmd send_pointer_input_cmd;

  fuchsia::ui::input::Command input_cmd;
  input_cmd.set_send_pointer_input(send_pointer_input_cmd);

  fuchsia::ui::scenic::Command cmd;
  cmd.set_input(std::move(input_cmd));

  session_->Enqueue(std::move(cmd));

  EXPECT_EQ(session_->num_flushed_, 1);
  EXPECT_EQ(session_->num_commands(), 0u);
  EXPECT_EQ(session_->num_bytes(),
            // See commands_sizing_test.cc for details.
            scenic::kEnqueueRequestBaseNumBytes + 104u);
  EXPECT_EQ(session_->num_handles(),
            // See commands_sizing_test.cc for details.
            0u);
}

TEST_F(ScenicSessionFlushTest, AddTwoInputCommands) {
  for (int64_t i = 0; i < 2; ++i) {
    fuchsia::ui::input::SendPointerInputCmd send_pointer_input_cmd;

    fuchsia::ui::input::Command input_cmd;
    input_cmd.set_send_pointer_input(send_pointer_input_cmd);

    fuchsia::ui::scenic::Command cmd;
    cmd.set_input(std::move(input_cmd));

    session_->Enqueue(std::move(cmd));
  }

  EXPECT_EQ(session_->num_flushed_, 2);
  EXPECT_EQ(session_->num_commands(), 0u);

  // The overriden Flush does not reset byte and handle counts, so we accumulate each
  // command's size.
  EXPECT_EQ(session_->num_bytes(),
            // See commands_sizing_test.cc for details.
            scenic::kEnqueueRequestBaseNumBytes + 2 * 104u);
  EXPECT_EQ(session_->num_handles(),
            // See commands_sizing_test.cc for details.
            0u);
}

TEST_F(ScenicSessionFlushTest, AddTenNonInputCommandsThenOneInputCommand) {
  for (int64_t i = 0; i < 10; ++i) {
    fuchsia::ui::gfx::MemoryArgs memory_args;

    fuchsia::ui::gfx::ResourceArgs resource_args;
    resource_args.set_memory(std::move(memory_args));

    fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd;
    create_resource_cmd.resource = std::move(resource_args);

    fuchsia::ui::gfx::Command gfx_cmd;
    gfx_cmd.set_create_resource(std::move(create_resource_cmd));

    fuchsia::ui::scenic::Command cmd;
    cmd.set_gfx(std::move(gfx_cmd));

    session_->Enqueue(std::move(cmd));
  }

  // We accumulate all commands (without ever flushing).
  EXPECT_EQ(session_->num_flushed_, 0u);
  EXPECT_EQ(session_->num_commands(), 10u);
  EXPECT_EQ(session_->num_bytes(),
            // See commands_sizing_test.cc for details.
            scenic::kEnqueueRequestBaseNumBytes + 10 * 104u);
  EXPECT_EQ(session_->num_handles(),
            // See commands_sizing_test.cc for details.
            10 * 1u);

  {
    fuchsia::ui::input::SendPointerInputCmd send_pointer_input_cmd;

    fuchsia::ui::input::Command input_cmd;
    input_cmd.set_send_pointer_input(send_pointer_input_cmd);

    fuchsia::ui::scenic::Command cmd;
    cmd.set_input(std::move(input_cmd));

    session_->Enqueue(std::move(cmd));
  }

  // Since input commands are eagerly flushed, we send all eleven commands at
  // once.
  EXPECT_EQ(session_->num_flushed_, 1u);
  EXPECT_EQ(session_->num_commands(), 0u);
  EXPECT_EQ(session_->num_bytes(),
            // See commands_sizing_test.cc for details.
            scenic::kEnqueueRequestBaseNumBytes + 11 * 104u);
  EXPECT_EQ(session_->num_handles(),
            // See commands_sizing_test.cc for details.
            10 * 1u);
}

TEST_F(ScenicSessionFlushTest, AddNonInputCommandThenPresent) {
  // Enqueue CreateResourceCmd
  {
    fuchsia::ui::gfx::MemoryArgs memory_args;

    fuchsia::ui::gfx::ResourceArgs resource_args;
    resource_args.set_memory(std::move(memory_args));

    fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd;
    create_resource_cmd.resource = std::move(resource_args);

    fuchsia::ui::gfx::Command gfx_cmd;
    gfx_cmd.set_create_resource(std::move(create_resource_cmd));

    fuchsia::ui::scenic::Command cmd;
    cmd.set_gfx(std::move(gfx_cmd));

    session_->Enqueue(std::move(cmd));
  }

  // The enqueued command has not been flushed, but will be when we present.
  EXPECT_EQ(session_->num_flushed_, 0);

  // Present
  auto callback = [&](fuchsia::images::PresentationInfo info) {};
  session_->Present(0u, std::move(callback));

  // And check that we've flushed.
  EXPECT_EQ(session_->num_flushed_, 1);
}

TEST_F(ScenicSessionFlushTest, AddNonInputCommandThenPresent2) {
  // Enqueue CreateResourceCmd
  {
    fuchsia::ui::gfx::MemoryArgs memory_args;

    fuchsia::ui::gfx::ResourceArgs resource_args;
    resource_args.set_memory(std::move(memory_args));

    fuchsia::ui::gfx::CreateResourceCmd create_resource_cmd;
    create_resource_cmd.resource = std::move(resource_args);

    fuchsia::ui::gfx::Command gfx_cmd;
    gfx_cmd.set_create_resource(std::move(create_resource_cmd));

    fuchsia::ui::scenic::Command cmd;
    cmd.set_gfx(std::move(gfx_cmd));

    session_->Enqueue(std::move(cmd));
  }

  // The enqueued command has not been flushed, but will be when we present.
  EXPECT_EQ(session_->num_flushed_, 0);

  // Present2
  auto callback = [&](fuchsia::ui::composition::FuturePresentationTimes info) {};
  session_->Present2(0u, 0u, std::move(callback));

  // And check that we've flushed.
  EXPECT_EQ(session_->num_flushed_, 1);
}

}  // namespace
