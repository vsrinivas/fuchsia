// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_OPERATION_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_OPERATION_TEST_LIB_H_

#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>
#include <thread>

#include <grpc/support/log.h>

#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

#include <grpc++/grpc++.h>

// Helper macro used in tests to wait for next event on GRPC completion queues
// and assert on the returned values.
#define ASSERT_GRPC_CQ_NEXT(cq, expect_tag, expect_ok) \
  {                                                    \
    bool ok__;                                         \
    void* tag__;                                       \
    ASSERT_TRUE((cq)->Next(&tag__, &ok__));            \
    ASSERT_EQ(tag__, static_cast<void*>(expect_tag));  \
    ASSERT_EQ(ok__, expect_ok);                        \
  }

class AsyncEndToEndTest : public testing::Test {
 protected:
  AsyncEndToEndTest()
      : client_cq_(std::make_unique<grpc::CompletionQueue>()),
        service_(std::make_unique<GuestInteractionService::AsyncService>()) {
    // Setup server
    grpc::ServerBuilder builder;
    builder.RegisterService(service_.get());
    server_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    // Setup stub
    grpc::ChannelArguments args;
    std::shared_ptr<grpc::Channel> channel = server_->InProcessChannel(args);
    stub_ = GuestInteractionService::NewStub(channel);
  }

  void TearDown() override {
    server_->Shutdown();
    void* tag;
    bool regular_event;

    server_cq_->Shutdown();
    while (server_cq_->Next(&tag, &regular_event)) {
      static_cast<CallData*>(tag)->Proceed(false);
      EXPECT_FALSE(regular_event) << "Unexpected remaining event in server cq";
    }

    client_cq_->Shutdown();
    while (client_cq_->Next(&tag, &regular_event)) {
      EXPECT_FALSE(regular_event) << "Unexpected remaining event in client cq";
    }
  }

  void RunLoopUntil(fit::function<bool()> check) {
    constexpr zx::duration kLoopStep = zx::msec(10);
    while (!check()) {
      zx_status_t status = loop_.Run(zx::deadline_after(kLoopStep), true);
      ASSERT_TRUE(status == ZX_ERR_TIMED_OUT || status == ZX_OK)
          << "Failed to run loop " << zx_status_get_string(status);
    }
  }

  const std::unique_ptr<grpc::CompletionQueue> client_cq_;
  const std::unique_ptr<GuestInteractionService::AsyncService> service_;
  std::unique_ptr<grpc::ServerCompletionQueue> server_cq_;
  std::unique_ptr<GuestInteractionService::Stub> stub_;
  std::unique_ptr<grpc::Server> server_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigAttachToCurrentThread);
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_OPERATION_TEST_LIB_H_
