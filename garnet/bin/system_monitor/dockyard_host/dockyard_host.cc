// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"

using dockyard::Greeter;
using dockyard::HelloReply;
using dockyard::HelloRequest;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

constexpr char DEFAULT_SERVER_ADDRESS[] = "0.0.0.0:50051";

// Logic and data behind the server's behavior.
class DockyardServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

// Listen for Harvester connections from the Fuchsia device.
void RunGrpcServer(const char* listen_at) {
  // This is an arbitrary default port.
  std::string server_address(listen_at);
  DockyardServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to a *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  std::cout << "Starting dockyard host" << std::endl;
  RunGrpcServer(DEFAULT_SERVER_ADDRESS);

  return 0;
}
