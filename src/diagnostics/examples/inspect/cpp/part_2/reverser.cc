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

  // [START part_1_callback]
  callback(std::move(output));
  // [END part_1_callback]
  response_count_.Add(1);
}

// [START part_1_add_connection_count]
// [START part_1_update_server]
fidl::InterfaceRequestHandler<fuchsia::examples::inspect::Reverser> Reverser::CreateDefaultHandler(
    inspect::Node node) {
  // [END part_1_update_server]
  // [START_EXCLUDE]
  auto global_request_count =
      std::make_shared<inspect::UintProperty>(node.CreateUint("total_requests", 0));
  // [END_EXCLUDE]

  // Return a handler for incoming FIDL connections to Reverser.
  //
  // The returned closure contains a binding set, which is used to bind incoming requests to a
  // particular implementation of a FIDL interface. This particular binding set is configured to
  // bind incoming requests to unique_ptr<Reverser>, which means the binding set itself takes
  // ownership of the created Reversers and frees them when the connection is closed.
  return [connection_count = node.CreateUint("connection_count", 0), node = std::move(node),
          // [START_EXCLUDE]
          global_request_count,
          // END_EXCLUDE]
          binding_set =
              std::make_unique<fidl::BindingSet<ReverserProto, std::unique_ptr<Reverser>>>()](
             fidl::InterfaceRequest<ReverserProto> request) mutable {
    connection_count.Add(1);
    // [END part_1_add_connection_count]

    // Create a stats structure for the new reverser.
    // [START part_1_connection_child]
    auto child = node.CreateChild(node.UniqueName("connection-"));
    // [END part_1_connection_child]
    ReverserStats stats{.connection_node = std::move(child),
                        .global_request_count = global_request_count};
    binding_set->AddBinding(std::make_unique<Reverser>(std::move(stats)), std::move(request));
  };
}
