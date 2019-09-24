// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
#define SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_

#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "src/lib/files/unique_fd.h"

namespace component {
namespace DynamicLibraryLoader {

zx_status_t Start(fbl::unique_fd fd, zx::channel* result);

}  // namespace DynamicLibraryLoader
}  // namespace component

#endif  // SRC_SYS_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
