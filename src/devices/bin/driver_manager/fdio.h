// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_

#include <fidl/fuchsia.io/cpp/wire.h>

class FsProvider {
  // Pure abstract interface describing how to get a clone of a channel to an fs handle.
 public:
  virtual ~FsProvider() = default;

  // Opens a path relative to locally-specified roots.
  //
  // This acts similar to 'open', but avoids utilizing the local process' namespace.
  // Instead, it manually translates hardcoded paths, such as "svc", "dev", etc into
  // their corresponding root connection, where the request is forwarded.
  //
  // This function is implemented by both devmgr and fshost.
  virtual fidl::ClientEnd<fuchsia_io::Directory> CloneFs(const char* path) = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_
