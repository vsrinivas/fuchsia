// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_IMPL_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_IMPL_H_

#include <grpc/support/log.h>

#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"
#include "src/virtualization/lib/guest_interaction/server/server_operation_state.h"

#include <grpc++/grpc++.h>

// Defined in Linux:include/uapi/linux/vm_sockets.h
#define VMADDR_CID_ANY (-1U)

template <class T>
class ServerImpl {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    cq_->Shutdown();
  }

  void Run() {
    grpc::ServerBuilder builder;
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    int sockfd = platform_interface_.GetServerFD(VMADDR_CID_ANY, GUEST_INTERACTION_PORT);
    std::cout << "Listening on fd=" << sockfd << std::endl;

    new ExecCallData<T>(&service_, cq_.get());
    new GetCallData<T>(&service_, cq_.get());
    new PutCallData<T>(&service_, cq_.get());

    void* tag;
    bool ok;
    constexpr gpr_timespec deadline = {
        .tv_nsec = 100 * 1000,  // 100ms.
        .clock_type = GPR_TIMESPAN,
    };

    while (true) {
      if (int fd = platform_interface_.Accept(sockfd); fd < 0) {
        if (errno != EAGAIN) {
          std::cerr << "Accept failed"
                    << " fd=" << fd << " errno=" << strerror(errno) << std::endl;
        }
      } else {
        std::cout << "Serving on fd=" << fd << std::endl;
        grpc::AddInsecureChannelFromFd(server_.get(), fd);
      }

      switch (cq_->AsyncNext(&tag, &ok, deadline)) {
        case grpc::CompletionQueue::SHUTDOWN:
          std::cerr << "completion queue shutdown" << std::endl;
          abort();
        case grpc::CompletionQueue::GOT_EVENT:
          static_cast<CallData*>(tag)->Proceed(ok);
          break;
        case grpc::CompletionQueue::TIMEOUT:
          break;
      };
    }
  }

 private:
  std::unique_ptr<grpc::ServerCompletionQueue> cq_;
  GuestInteractionService::AsyncService service_;
  std::unique_ptr<grpc::Server> server_;
  T platform_interface_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_IMPL_H_
