// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_DATA_PIPE_BLOCKING_COPY_H_
#define LIB_MTL_DATA_PIPE_BLOCKING_COPY_H_

#include <functional>

#include "mojo/public/cpp/system/data_pipe.h"

namespace mtl {

bool BlockingCopyFrom(
    mojo::ScopedDataPipeConsumerHandle source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes);

}  // namespace mtl

#endif  // LIB_MTL_DATA_PIPE_BLOCKING_COPY_H_
