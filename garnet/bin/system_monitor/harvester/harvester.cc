// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
    Hey, so there's not a whole lot here. That's because this is a work in
    progress. Starting from something like a hello world program, this will
    progress into a System Monitor for Fuchsia.

    The code below is largely straight out of the grpc hello world example.

    See also: ./README.md
*/

#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"

using dockyard_proto::Dockyard;
using dockyard_proto::InitReply;
using dockyard_proto::InitRequest;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class Harvester {
 public:
  Harvester(std::shared_ptr<Channel> channel)
      : stub_(Dockyard::NewStub(channel)) {}

  std::string Init() {
    // Data we are sending to the server.
    InitRequest request;
    request.set_name("test");

    // Container for the data we expect from the server.
    InitReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->Init(&context, request, &reply);
    if (status.ok()) {
      return "Init";
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "Unable to send to dockyard.";
    }
  }

 private:
  std::unique_ptr<Dockyard::Stub> stub_;
};

int main(int argc, char** argv) {
  printf("System Monitor Harvester - wip 5\n");
  if (argc < 2) {
    std::cout << "Please specify an IP:Port, such as localhost:50051"
              << std::endl;
    exit(1);
  }
  // TODO(dschuyler): This channel isn't authenticated
  // (InsecureChannelCredentials()).
  Harvester harvester(
      grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials()));
  std::string reply = harvester.Init();
  std::cout << "harvester received: " << reply << std::endl;

  return 0;
}
