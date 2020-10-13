// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_4_REVERSER_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_4_REVERSER_H_

#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

// Reverser stats encapsulates all stat input to the Reverser.
struct ReverserStats {
  // The node for an individual connection to the Reverser service.
  inspect::Node connection_node;

  // Global property for request count.
  // Updating properties is thread-safe.
  std::shared_ptr<inspect::UintProperty> global_request_count;

  // Creates a default ReverserStats on which all operations are no-ops.
  static ReverserStats CreateDefault() {
    return ReverserStats{.connection_node = inspect::Node(),
                         .global_request_count = std::make_shared<inspect::UintProperty>()};
  }
};

// Implementation of the fuchsia.examples.inspect.Reverser protocol.
//
// Each instantiation of this class handles a single connection to Reverser.
class Reverser final : public fuchsia::examples::inspect::Reverser {
 public:
  // Construct a new reverser that exposes stats.
  Reverser(ReverserStats stats);

  // Implementation of Reverser.Reverse().
  void Reverse(std::string input, ReverseCallback callback) override;

  // Return a request handler for the Reverser protocol that binds incoming requests to new
  // Reversers.
  //
  // |node| is an inspect Node to store stat information under.
  static fidl::InterfaceRequestHandler<fuchsia::examples::inspect::Reverser> CreateDefaultHandler(
      inspect::Node node);

 private:
  // The incoming stats nodes for this property.
  ReverserStats stats_;

  // Local request and response counts.
  inspect::UintProperty request_count_, response_count_;
};

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_4_REVERSER_H_
