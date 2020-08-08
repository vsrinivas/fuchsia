// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
#define SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fbl/unique_fd.h>

namespace component {
namespace DynamicLibraryLoader {

// package_fd should be an open fd for a package directory. This function will open the "lib/"
// subdirectory automatically when creating the loader service, so it does not need ownership of
// package_fd.
zx::status<zx::channel> Start(int package_fd, std::string name);

}  // namespace DynamicLibraryLoader
}  // namespace component

#endif  // SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
