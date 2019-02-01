// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connect.h"

#include <lib/fdio/util.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/substitute.h>
#include <zircon/device/vfs.h>

namespace iquery {

Connection::Connection(const std::string& directory_path)
    : directory_path_(directory_path) {}

bool Connection::Validate() {
  return files::IsFile(
      files::AbsolutePath(fxl::Concatenate({directory_path_, "/.channel"})));
}

zx_status_t Connection::Connect(
    fidl::InterfaceRequest<fuchsia::inspect::Inspect> request) {
  return fdio_service_connect(
      files::AbsolutePath(fxl::Concatenate({directory_path_, "/.channel"}))
          .c_str(),
      request.TakeChannel().release());
}

fuchsia::inspect::InspectSyncPtr Connection::SyncOpen() {
  fuchsia::inspect::InspectSyncPtr ret;
  zx_status_t status = Connect(ret.NewRequest());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to connect: " << status;
    ret.Unbind();
  }
  return ret;
}

fuchsia::inspect::InspectPtr Connection::Open() {
  fuchsia::inspect::InspectPtr ret;
  zx_status_t status = Connect(ret.NewRequest());
  if (status != ZX_OK) {
    FXL_VLOG(1) << "Failed to connect: " << status;
    ret.Unbind();
  }
  return ret;
}

}  // namespace iquery
