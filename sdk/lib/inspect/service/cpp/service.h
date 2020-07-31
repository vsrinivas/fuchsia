// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_SERVICE_CPP_SERVICE_H_
#define LIB_INSPECT_SERVICE_CPP_SERVICE_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

namespace inspect {

struct TreeHandlerSettings {
  // If true, snapshots of trees returned by the handler must be private
  // copies. Setting this option disables VMO sharing between a reader
  // and the writer.
  bool force_private_snapshot = false;
};

// Returns a handler for fuchsia.inspect.Tree connections on the given Inspector.
//
// This is meant to be used to construct a vfs::Service Node to serve the given Inspector as a
// fuchsia.inspect.Tree.
fidl::InterfaceRequestHandler<fuchsia::inspect::Tree> MakeTreeHandler(
    const inspect::Inspector* inspector, async_dispatcher_t* dispatcher = nullptr,
    TreeHandlerSettings settings = {});

}  // namespace inspect

#endif  // LIB_INSPECT_SERVICE_CPP_SERVICE_H_
