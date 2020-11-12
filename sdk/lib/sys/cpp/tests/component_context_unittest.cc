// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/spawn.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <zircon/processargs.h>

#include <gtest/gtest.h>

#include "echo_server.h"

class ComponentContextTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &outgoing_client_, &outgoing_server_));
  }

 protected:
  void QueryEcho() {
    fdio_service_connect_at(outgoing_client_.get(), "svc/test.placeholders.Echo",
                            echo_client_.NewRequest(dispatcher()).TakeChannel().release());

    echo_client_->EchoString("hello", [this](fidl::StringPtr value) { echo_result_ = *value; });
  }

  void PublishEcho() {
    ASSERT_EQ(ZX_OK, context_->outgoing()->AddPublicService(echo_impl_.GetHandler(dispatcher())));
  }

  zx::channel outgoing_client_;
  zx::channel outgoing_server_;
  std::unique_ptr<sys::ComponentContext> context_;

  EchoImpl echo_impl_;
  test::placeholders::EchoPtr echo_client_;
  std::string echo_result_ = "no callback";
};

TEST_F(ComponentContextTest, ServeInConstructor) {
  // Try connecting to a service and call it before it's published.
  QueryEcho();

  // Starts serving outgoing directory immediately. Will process Echo request
  // the next time async loop will run.
  context_ = std::make_unique<sys::ComponentContext>(sys::ServiceDirectory::CreateFromNamespace(),
                                                     std::move(outgoing_server_));

  // Now publish the service. It's not too late as long as the run loop hasn't
  // run after ComponentContext creation.
  PublishEcho();

  // Echo connection requests should be processed now.
  RunLoopUntilIdle();

  EXPECT_EQ("hello", echo_result_);
}

TEST_F(ComponentContextTest, DelayedServe) {
  // Doesn't start serving outgoing directory.
  context_ = std::make_unique<sys::ComponentContext>(sys::ServiceDirectory::CreateFromNamespace());

  // Try connecting to a service and call it before it's published.
  QueryEcho();
  RunLoopUntilIdle();

  // Now publish the service and start serving the directory.
  PublishEcho();
  context_->outgoing()->Serve(std::move(outgoing_server_));

  RunLoopUntilIdle();

  EXPECT_EQ("hello", echo_result_);
}

class ComponentContextStaticConstructorTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    gtest::RealLoopFixture::SetUp();
    // vfs_.SetDispatcher(async_get_default_dispatcher());

    // Create a child job in which to launch our test process.
    zx_status_t status = zx::job::create(*zx::job::default_job(), 0u, &job_);
    ASSERT_EQ(status, ZX_OK);
  }

  int RunHelperProc(std::vector<std::string> args, zx::channel out_dir_req) {
    std::vector<const char*> argv_ptr;
    argv_ptr.push_back("constructor_helper_proc");
    for (auto& arg : args) {
      argv_ptr.push_back(arg.data());
    }
    argv_ptr.push_back(nullptr);

    // Provide the child process with the out directory request |out_dir_req|.
    std::vector<fdio_spawn_action_t> actions;
    actions.push_back({.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
                       .h = {.id = PA_DIRECTORY_REQUEST, .handle = out_dir_req.release()}});

    // Place a channel at /dont_error: the helper process will error if it does not
    // encounter *anything* at this path.
    zx::channel ns_dont_error, ns_dont_error_req;
    zx::channel::create(0, &ns_dont_error, &ns_dont_error_req);
    actions.push_back({.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                       .ns = {.prefix = "/dont_error", .handle = ns_dont_error.release()}});

    // Create the child helper process.
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx::process process;
    auto status = fdio_spawn_etc(job_.get(), FDIO_SPAWN_CLONE_ALL,
                                 "/pkg/bin/constructor_helper_proc", argv_ptr.data(),
                                 /*environ=*/nullptr, actions.size(), actions.data(),
                                 process.reset_and_get_address(), err_msg);
    EXPECT_EQ(status, ZX_OK) << err_msg;

    // Run the process until termination and return the exit code.
    auto result =
        process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), /*pending=*/nullptr);
    EXPECT_EQ(result, ZX_OK);
    zx_info_process_t process_info = {};
    result = process.get_info(ZX_INFO_PROCESS, &process_info, sizeof(process_info), NULL, NULL);
    EXPECT_EQ(result, ZX_OK);

    // Allow the run loop to drain so that any messages that constructor_helper_proc
    // sent are processed.
    RunLoopUntilIdle();
    return process_info.return_code;
  }

  void TearDown() override {
    if (job_) {
      job_.kill();
    }
    gtest::RealLoopFixture::TearDown();
  }

 protected:
  zx::job job_;
};

// Show that ComponentContgext::CreateAndServeOutgoingDirectory() correctly
// wires up the process's PA_DIRECTORY_REQUEST (outgoing directory) and
// incoming namespace.
TEST_F(ComponentContextStaticConstructorTest, Create_ServeOutImmediately) {
  zx::channel out_dir, out_dir_req;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &out_dir, &out_dir_req));

  // The process publishes a service at svc/test.placeholders.Echo. Once a request
  // to open that path succeeds, the process's out dir has been published correctly.
  test::placeholders::EchoPtr echo_client;
  auto status =
      fdio_service_connect_at(out_dir.get(), "svc/test.placeholders.Echo",
                              echo_client.NewRequest(dispatcher()).TakeChannel().release());
  EXPECT_EQ(ZX_OK, status);

  bool got_echo_result = false;
  echo_client->EchoString("hello", [&](fidl::StringPtr value) { got_echo_result = true; });

  EXPECT_EQ(0, RunHelperProc({}, std::move(out_dir_req)));
  EXPECT_TRUE(got_echo_result);
}

// Equivalent to above, but instructs the helper proc to delay populating and
// serving the out directory for 50ms in a future iteration of the run loop.
TEST_F(ComponentContextStaticConstructorTest, Create_ServeOutDelayed) {
  zx::channel out_dir, out_dir_req;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &out_dir, &out_dir_req));

  test::placeholders::EchoPtr echo_client;
  auto status =
      fdio_service_connect_at(out_dir.get(), "svc/test.placeholders.Echo",
                              echo_client.NewRequest(dispatcher()).TakeChannel().release());
  EXPECT_EQ(ZX_OK, status);

  bool got_echo_result = false;
  echo_client->EchoString("hello", [&](fidl::StringPtr value) { got_echo_result = true; });

  EXPECT_EQ(0, RunHelperProc({"--serve_out_delay_50ms"}, std::move(out_dir_req)));
  EXPECT_TRUE(got_echo_result);
}
