// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_TEST_LIB_H_

#include <errno.h>
#include <fcntl.h>
#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <netinet/in.h>
#include <src/lib/fxl/logging.h>
#include <sys/socket.h>

#include <iostream>
#include <thread>

#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

// Adapted from gRPC's async_end2end_test.cc
class TestScenario {
 public:
  TestScenario(bool non_block, bool inproc_stub, const grpc::string& creds_type,
               const grpc::string& content)
      : disable_blocking(non_block),
        inproc(inproc_stub),
        credentials_type(creds_type),
        message_content(content) {}
  void Log() const;
  bool disable_blocking;
  bool inproc;
  const grpc::string credentials_type;
  const grpc::string message_content;
};

static std::ostream& operator<<(std::ostream& out, const TestScenario& scenario) {
  return out << "TestScenario{disable_blocking=" << (scenario.disable_blocking ? "true" : "false")
             << ", inproc=" << (scenario.inproc ? "true" : "false") << ", credentials='"
             << scenario.credentials_type << "', message_size=" << scenario.message_content.size()
             << "}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  gpr_log(GPR_DEBUG, "%s", out.str().c_str());
}

class AsyncEndToEndTest : public testing::TestWithParam<TestScenario> {
 protected:
  AsyncEndToEndTest() {}

  void SetUp() override {
    client_cq_ = std::make_unique<grpc::CompletionQueue>();

    // Setup server
    BuildAndStartServer();
  }

  void TearDown() override {
    server_->Shutdown();

    // The server shutdown calls shutdown on the server's CompletionQueue.  The
    // client's CompletionQueue needs to be cleaned up manually.
    void* ignored_tag;
    bool ignored_ok;
    client_cq_->Shutdown();
    while (client_cq_->Next(&ignored_tag, &ignored_ok))
      ;

    stub_.reset();
  }

  void BuildAndStartServer() {
    grpc::ServerBuilder builder;
    service_.reset(new GuestInteractionService::AsyncService());
    builder.RegisterService(service_.get());
    server_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void ResetStub() {
    grpc::ChannelArguments args;
    std::shared_ptr<grpc::Channel> channel = server_->InProcessChannel(args);
    stub_ = GuestInteractionService::NewStub(channel);
  }

  std::unique_ptr<grpc::ServerCompletionQueue> server_cq_;
  std::unique_ptr<grpc::CompletionQueue> client_cq_;
  std::unique_ptr<GuestInteractionService::Stub> stub_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<GuestInteractionService::AsyncService> service_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_TEST_LIB_H_
