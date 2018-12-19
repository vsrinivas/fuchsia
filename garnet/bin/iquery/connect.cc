// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "connect.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/split_string.h>
#include <lib/fxl/strings/substitute.h>
#include <zircon/device/vfs.h>

namespace iquery {

namespace {
std::string InspectServicePath(const std::string& directory) {
  return fxl::Concatenate({files::AbsolutePath(directory), "/",
                           fxl::StringView(fuchsia::inspect::Inspect::Name_)});
}

// Connect to the Inspect interface on a path that may end within the
// inspect hierarchy itself. For example, if the file hub/objects is an
// entry point for inspect, and objects contains a child "child_object",
// this function allows opening "/hub/objects/child_object" by path.
zx_status_t ConnectToPath(const std::string& in_path,
                          fuchsia::inspect::InspectSyncPtr* out_ptr) {
  std::string path = files::AbsolutePath(in_path);
  auto pieces = fxl::SplitString(path, "/", fxl::kTrimWhitespace,
                                 fxl::kSplitWantNonEmpty);
  fuchsia::inspect::InspectSyncPtr inspect_ptr;
  std::string current_path = "/";
  for (const auto& piece : pieces) {
    if (inspect_ptr.is_bound()) {
      // If the request has already been open, recurse by opening children over
      // the API.
      fuchsia::inspect::InspectSyncPtr next_request;
      bool ok;
      inspect_ptr->OpenChild({piece.begin(), piece.length()},
                             next_request.NewRequest(), &ok);
      if (!ok) {
        return ZX_ERR_NOT_FOUND;
      }
      inspect_ptr = std::move(next_request);
    } else {
      // If the request has not been open, recurse by going down a directory
      // level. Once an Inspect directory is found, open the request so that
      // recursing can continue within the API.
      current_path.append({piece.data(), piece.size()});
      current_path.append("/");
      if (files::IsFile(InspectServicePath(current_path))) {
        zx_status_t status = fdio_service_connect(
            InspectServicePath(current_path).c_str(),
            inspect_ptr.NewRequest().TakeChannel().release());
        if (status != ZX_OK) {
          return status;
        }
      }
    }
  }

  if (inspect_ptr.is_bound()) {
    if (out_ptr) {
      *out_ptr = std::move(inspect_ptr);
    }
    return ZX_OK;
  }
  return ZX_ERR_INVALID_ARGS;
}  // namespace

}  // namespace

Connection::Connection(const std::string& directory_path)
    : directory_path_(directory_path) {}

bool Connection::Validate() {
  if (files::IsFile(InspectServicePath(directory_path_))) {
    return true;
  }

  return ConnectToPath(directory_path_, nullptr) == ZX_OK;
}

zx_status_t Connection::Connect(
    fidl::InterfaceRequest<fuchsia::inspect::Inspect> request) {
  if (files::IsFile(InspectServicePath(directory_path_))) {
    return fdio_service_connect(InspectServicePath(directory_path_).c_str(),
                                request.TakeChannel().release());
  }

  auto parts = fxl::SplitString(directory_path_, "/", fxl::kTrimWhitespace,
                                fxl::kSplitWantNonEmpty);
  if (parts.size() == 0) {
    return ZX_ERR_NOT_FOUND;
  }

  auto last_part = parts.back();
  parts.pop_back();
  fuchsia::inspect::InspectSyncPtr parent;
  zx_status_t status = ConnectToPath(fxl::JoinStrings(parts, "/"), &parent);
  if (status != ZX_OK) {
    return status;
  }

  bool ok;
  parent->OpenChild({last_part.data(), last_part.size()}, std::move(request),
                    &ok);
  if (ok) {
    return ZX_OK;
  }
  return ZX_ERR_NOT_FOUND;
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
