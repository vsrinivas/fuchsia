// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_VNODE_BUFFER_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_VNODE_BUFFER_H_

#ifdef __Fuchsia__
#include "resizeable_vmo_buffer.h"
#else
#include "resizeable_array_buffer.h"
#endif

namespace minfs {

#ifdef __Fuchsia__
using VnodeBufferType = ResizeableVmoBuffer;
#else
using VnodeBufferType = ResizeableArrayBuffer;
#endif

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_VNODE_BUFFER_H_
