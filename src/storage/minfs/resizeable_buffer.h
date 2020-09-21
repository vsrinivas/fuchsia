// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_RESIZEABLE_BUFFER_H_
#define SRC_STORAGE_MINFS_RESIZEABLE_BUFFER_H_

#ifdef __Fuchsia__
#include "resizeable_vmo_buffer.h"
#else
#include "resizeable_array_buffer.h"
#endif

namespace minfs {

#ifdef __Fuchsia__
using ResizeableBufferType = ResizeableVmoBuffer;
#else
using ResizeableBufferType = ResizeableArrayBuffer;
#endif

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_RESIZEABLE_BUFFER_H_
