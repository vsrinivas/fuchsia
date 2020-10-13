// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reverser.h"

#include <lib/fidl/cpp/binding_set.h>

Reverser::Reverser(ReverserStats stats)
    : stats_(std::move(stats)),
      request_count_(stats_.connection_node.CreateUint("request_count", 0)),
      response_count_(stats_.connection_node.CreateUint("response_count", 0)) {}

using ReverserProto = fuchsia::examples::inspect::Reverser;

void Reverser::Reverse(std::string input, ReverseCallback callback) {
  stats_.global_request_count->Add(1);
  request_count_.Add(1);

  std::string output;
  for (auto it = input.rbegin(); it != input.rend(); ++it) {
    output.push_back(*it);
  }

  callback(std::move(output));
  response_count_.Add(1);
}

fidl::InterfaceRequestHandler<fuchsia::examples::inspect::Reverser> Reverser::CreateDefaultHandler(
    inspect::Node node) {
  auto global_request_count =
      std::make_shared<inspect::UintProperty>(node.CreateUint("total_requests", 0));

  // Return a handler for incoming FIDL connections to Reverser.
  //
  // The returned closure contains a binding set, which is used to bind incoming requests to a
  // particular implementation of a FIDL interface. This particular binding set is configured to
  // bind incoming requests to unique_ptr<Reverser>, which means the binding set itself takes
  // ownership of the created Reversers and frees them when the connection is closed.
  return [connection_count = node.CreateUint("connection_count", 0), node = std::move(node),
          global_request_count,
          binding_set =
              std::make_unique<fidl::BindingSet<ReverserProto, std::unique_ptr<Reverser>>>()](
             fidl::InterfaceRequest<ReverserProto> request) mutable {
    connection_count.Add(1);

    // Create a stats structure for the new reverser.
    ReverserStats stats{.connection_node = node.CreateChild(node.UniqueName("connection-")),
                        .global_request_count = global_request_count};
    binding_set->AddBinding(std::make_unique<Reverser>(std::move(stats)), std::move(request));
  };
}
