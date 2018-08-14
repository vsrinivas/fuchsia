// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_CONNECT_H_
#define GARNET_BIN_IQUERY_CONNECT_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/fxl/macros.h>

namespace iquery {

class Connection {
 public:
  Connection(const std::string& directory_path);

  // Pre-validate that the connection seems valid.
  bool Validate();

  // Open a SyncPtr to the inspect interface exposed on the path.
  fuchsia::inspect::InspectSyncPtr SyncOpen();

  // Open an Async Ptr to the inspect interface exposed on the path.
  fuchsia::inspect::InspectPtr Open();

 private:
  // Connect to the path, returning the status of the connection.
  zx_status_t Connect(
      fidl::InterfaceRequest<fuchsia::inspect::Inspect> request);

  // The directory path to connect to.
  std::string directory_path_;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_CONNECT_H_
