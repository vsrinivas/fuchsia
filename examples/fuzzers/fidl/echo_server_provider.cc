// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/cpp/fidl.h>
#include <lib/fidl/cpp/fuzzing/server_provider.h>

// A simple implementation of the Echo protocol, to be used as a fuzz target.
class EchoImpl : public fuchsia::examples::Echo {
 public:
  void EchoString(std::string value, EchoStringCallback callback) override { callback(value); }
  void SendString(std::string value) override {}
};

// Use the provided macro to instantiate a `ServerProvider` and associated C symbols for linking
// against a FIDL server implementation fuzzer.
FIDL_FUZZER_DEFINITION(
    // Use the default `ServerProvider`; no overrides needed to fuzz this serviice implementation.
    ::fidl::fuzzing::ServerProvider,
    // Define a fuzzer for the abstract FIDL server class `Echo`.
    ::fuchsia::examples::Echo,
    // Define a fuzzer for the concrete FIDL server implementation `EchoImpl`.
    EchoImpl,
    // Use the thread/loop/dispatcher from the `ServerProvider.Connect()` caller; that is, dispatch
    // client and server work from the same thread/loop/dispatcher.
    ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller,
    // All remaining parameters forwarded to the `EchoServer` constructor.
);
