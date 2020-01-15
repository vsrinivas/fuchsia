// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reverser.h"

#include <lib/fidl/cpp/binding_set.h>

using ReverserProto = fuchsia::examples::inspect::Reverser;

void Reverser::Reverse(std::string input, ReverseCallback callback) {
  // CODELAB: Add stats about incoming requests.
  std::string output;
  for (auto it = input.rbegin(); it != input.rend(); ++it) {
    output.push_back(*it);
  }
}

fidl::InterfaceRequestHandler<fuchsia::examples::inspect::Reverser>
Reverser::CreateDefaultHandler() {
  // Return a handler for incoming FIDL connections to Reverser.
  //
  // The returned closure contains a binding set, which is used to bind incoming requests to a
  // particular implementation of a FIDL interface. This particular binding set is configured to
  // bind incoming requests to unique_ptr<Reverser>, which means the binding set itself takes
  // ownership of the created Reversers and frees them when the connection is closed.
  return [binding_set =
              std::make_unique<fidl::BindingSet<ReverserProto, std::unique_ptr<Reverser>>>()](
             fidl::InterfaceRequest<ReverserProto> request) mutable {
    // CODELAB: Add stats about incoming connections.
    binding_set->AddBinding(std::make_unique<Reverser>(), std::move(request));
  };
}
