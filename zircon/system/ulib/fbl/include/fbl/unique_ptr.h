// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_UNIQUE_PTR_H_
#define FBL_UNIQUE_PTR_H_

#include <fbl/alloc_checker.h>
#include <memory>

namespace fbl {

template <typename T>
using unique_ptr = std::unique_ptr<T>;

}  // namespace fbl

#endif  // FBL_UNIQUE_PTR_H_
