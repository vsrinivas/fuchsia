// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/driver_context.h>

#include <utility>

using OutgoingDirectory = component::OutgoingDirectory;

namespace driver {

DriverContext::DriverContext(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

DriverContext::~DriverContext() = default;

void DriverContext::InitializeAndServe(
    Namespace incoming, fidl::ServerEnd<fuchsia_io::Directory> outgoing_directory_request) {
  incoming_ = std::make_shared<Namespace>(std::move(incoming));
  outgoing_ = std::make_shared<OutgoingDirectory>(OutgoingDirectory::Create(dispatcher_));
  ZX_ASSERT(outgoing_->Serve(std::move(outgoing_directory_request)).is_ok());
}

}  // namespace driver
