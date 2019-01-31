// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_HANDLE_H_
#define LIB_ZX_HANDLE_H_

#include <lib/zx/object.h>

namespace zx {

using handle = object<void>;
using unowned_handle = unowned<handle>;

} // namespace zx

#endif  // LIB_ZX_HANDLE_H_
