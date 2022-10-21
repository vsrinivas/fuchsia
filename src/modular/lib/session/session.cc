// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/session/session.h"

#include <lib/fdio/directory.h>

#include "src/lib/files/glob.h"
#include "src/modular/lib/session/session_constants.h"

namespace modular::session {

namespace {

// Connects to a protocol served at the first path that matches one of the given glob patterns.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: No path exists that matches a pattern in |glob_paths|, or if connecting
// to a matching path was unsuccessful.
template <typename Interface, typename InterfacePtr = fidl::InterfacePtr<Interface>>
fpromise::result<InterfacePtr, zx_status_t> ConnectInPaths(
    std::initializer_list<std::string> glob_paths) {
  files::Glob glob(glob_paths);

  for (const std::string& path : glob) {
    InterfacePtr ptr;
    if (fdio_service_connect(path.c_str(), ptr.NewRequest().TakeChannel().get()) == ZX_OK) {
      return fpromise::ok(std::move(ptr));
    }
  }

  return fpromise::error(ZX_ERR_NOT_FOUND);
}

}  // namespace

fpromise::result<fuchsia::modular::internal::BasemgrDebugPtr, zx_status_t> ConnectToBasemgrDebug() {
  return ConnectInPaths<fuchsia::modular::internal::BasemgrDebug>({kBasemgrDebugSessionGlob});
}

}  // namespace modular::session
