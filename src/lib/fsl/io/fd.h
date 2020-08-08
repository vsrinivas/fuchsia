// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FSL_IO_FD_H_
#define SRC_LIB_FSL_IO_FD_H_

#include <lib/zx/channel.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/fxl_export.h"

namespace fsl {

FXL_EXPORT zx::channel CloneChannelFromFileDescriptor(int fd);

FXL_EXPORT zx::channel TransferChannelFromFileDescriptor(fbl::unique_fd fd);

FXL_EXPORT fbl::unique_fd OpenChannelAsFileDescriptor(zx::channel channel);

}  // namespace fsl

#endif  // SRC_LIB_FSL_IO_FD_H_
