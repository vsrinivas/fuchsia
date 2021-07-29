// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fidl/cpp/fuzzing/server_provider.h>
#include <lib/inspect/cpp/inspect.h>

#include "src/cobalt/bin/app/system_data_updater_impl.h"
#include "third_party/cobalt/src/system_data/system_data.h"

namespace {

cobalt::encoder::SystemData system_data("a", "b", cobalt::ReleaseStage::GA, "c");

}  // namespace

// Use the provided macro to instantiate a `ServerProvider` and associated C symbols for linking
// against a FIDL server implementation fuzzer.
FIDL_FUZZER_DEFINITION(
    // Use the default `ServerProvider`; no overrides needed to fuzz this serviice implementation.
    ::fidl::fuzzing::ServerProvider,
    // Define a fuzzer for the abstract FIDL server class `SystemDataUpdater`.
    ::fuchsia::cobalt::SystemDataUpdater,
    // Define a fuzzer for the concrete FIDL server implementation `SystemDataUpdaterImpl`.
    ::cobalt::SystemDataUpdaterImpl,
    // Use the thread/loop/dispatcher from the `ServerProvider.Connect()` caller; that is, dispatch
    // client and server work from the same thread/loop/dispatcher.
    ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller,
    // All remaining parameters forwarded to the `SystemDataUpdaterImpl` constructor.
    inspect::Node(), &system_data, "/tmp/cache");
