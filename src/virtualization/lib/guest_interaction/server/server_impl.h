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

template <class T>
class ServerImpl {
 public:
  ServerImpl(){};
  ~ServerImpl();
  void Run();

 private:
  int GetSocket();
  void AcceptClient(int sockfd);
  void HandleGetRequest();

  std::unique_ptr<grpc::ServerCompletionQueue> cq_;
  GuestInteractionService::AsyncService service_;
  std::unique_ptr<grpc::Server> server_;
  T platform_interface_;
};

template <class T>
ServerImpl<T>::~ServerImpl() {
  server_->Shutdown();
  cq_->Shutdown();
}

template <class T>
int ServerImpl<T>::GetSocket() {
  return platform_interface_.GetServerFD(VMADDR_CID_ANY, GUEST_INTERACTION_PORT);
}

template <class T>
void ServerImpl<T>::AcceptClient(int sockfd) {
  return platform_interface_.AcceptClient(server_.get(), sockfd);
}

template <class T>
void ServerImpl<T>::Run() {
  grpc::ServerBuilder builder;
  builder.RegisterService(&service_);
  cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();

  int sockfd = GetSocket();

  new ExecCallData<T>(&service_, cq_.get());
  new GetCallData<T>(&service_, cq_.get());
  new PutCallData<T>(&service_, cq_.get());

  void* tag;
  bool ok;
  gpr_timespec wait_time = {};

  while (true) {
    AcceptClient(sockfd);

    grpc::CompletionQueue::NextStatus status = cq_->AsyncNext(&tag, &ok, wait_time);
    GPR_ASSERT(status != grpc::CompletionQueue::SHUTDOWN);
    if (status == grpc::CompletionQueue::GOT_EVENT) {
      static_cast<CallData*>(tag)->Proceed(ok);
    }
  }
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_IMPL_H_
