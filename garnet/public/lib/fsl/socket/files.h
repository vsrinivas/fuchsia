// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_SOCKET_FILES_H_
#define LIB_FSL_SOCKET_FILES_H_

#include <functional>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/fxl_export.h"

namespace fsl {

// Asynchronously copies data from source to the destination file descriptor.
// The given |callback| is run upon completion. File writes and |callback| will
// be scheduled on the given |task_runner|.
FXL_EXPORT void CopyToFileDescriptor(
    zx::socket source, fxl::UniqueFD destination,
    async_dispatcher_t* dispatcher,
    fit::function<void(bool /*success*/, fxl::UniqueFD /*destination*/)>
        callback);

// Asynchronously copies data from source file to the destination. The given
// |callback| is run upon completion. File reads and |callback| will be
// scheduled to the given |task_runner|.
FXL_EXPORT void CopyFromFileDescriptor(
    fxl::UniqueFD source, zx::socket destination,
    async_dispatcher_t* dispatcher,
    fit::function<void(bool /*success*/, fxl::UniqueFD /*source*/)> callback);

}  // namespace fsl

#endif  // LIB_FSL_SOCKET_FILES_H_
