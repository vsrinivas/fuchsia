// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_IO_FD_H_
#define LIB_FSL_IO_FD_H_

#include <lib/zx/channel.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/fxl_export.h"

namespace fsl {

FXL_EXPORT zx::channel CloneChannelFromFileDescriptor(int fd);

FXL_EXPORT fxl::UniqueFD OpenChannelAsFileDescriptor(zx::channel channel);

}  // namespace fsl

#endif  // LIB_FSL_IO_REDIRECTION_H_
