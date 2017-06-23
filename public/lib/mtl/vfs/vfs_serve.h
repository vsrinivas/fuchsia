// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VFS_VFS_SERVE_H_
#define LIB_MTL_VFS_VFS_SERVE_H_

#include <fs/vfs.h>
#include <mx/channel.h>

#include "lib/ftl/ftl_export.h"

namespace mtl {

FTL_EXPORT bool VFSServe(mxtl::RefPtr<fs::Vnode> directory,
                         mx::channel request);

}  // namespace mtl

#endif  // LIB_MTL_VFS_VFS_SERVE_H_
