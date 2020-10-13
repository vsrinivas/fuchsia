// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_1_REVERSER_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_1_REVERSER_H_

#include <fuchsia/examples/inspect/cpp/fidl.h>

// Implementation of the fuchsia.examples.inspect.Reverser protocol.
//
// Each instantiation of this class handles a single connection to Reverser.
// [START reverser_h]
class Reverser final : public fuchsia::examples::inspect::Reverser {
 public:
  // CODELAB: Create a new constructor for Reverser that takes an Inspect node.

  // Implementation of Reverser.Reverse().
  void Reverse(std::string input, ReverseCallback callback) override;

  // Return a request handler for the Reverser protocol that binds incoming requests to new
  // Reversers.
  static fidl::InterfaceRequestHandler<fuchsia::examples::inspect::Reverser> CreateDefaultHandler();
};
// [END reverser_h]

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CODELAB_CPP_PART_1_REVERSER_H_
