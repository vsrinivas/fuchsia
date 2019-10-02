// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/fuzzing/server_provider.h>

#include "garnet/examples/fidl/echo_server_cpp/echo_server_app.h"

// Use the provided macro to instantiate a `ServerProvider` and associated C symbols for linking
// against a FIDL server implementation fuzzer.
FIDL_FUZZER_DEFINITION(
    // Use the default `ServerProvider`; no overrides needed to fuzz this serviice implementation.
    ::fidl::fuzzing::ServerProvider,
    // Define a fuzzer for the abstract FIDL server class `Echo`.
    ::fidl::examples::echo::Echo,
    // Define a fuzzer for the concrete FIDL server implementation `EchoServer`.
    ::echo::EchoServer,
    // Use the thread/loop/dispatcher from the `ServerProvider.Connect()` caller; that is, dispatch
    // client and server work from the same thread/loop/dispatcher.
    ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller,
    // All remaining parameters forwarded to the `EchoServer` constructor.
    // Parameters: `quiet`.
    false);
