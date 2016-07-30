// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_DATA_PIPE_FILES_H_
#define LIB_MTL_DATA_PIPE_FILES_H_

#include <stdio.h>

#include <string>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace mtl {

// Asynchronously copies data from source to the destination file descriptor.
// The given |callback| is run upon completion. File writes will be scheduled to
// the given |task_runner|.
void CopyToFileDescriptor(
    mojo::ScopedDataPipeConsumerHandle source,
    ftl::UniqueFD destination,
    ftl::TaskRunner* task_runner,
    const std::function<void(bool /*success*/)>& callback);

// Asynchronously copies data from source file to the destination. The given
// |callback| is run upon completion. File writes will be scheduled to the
// given |task_runner|.
void CopyFromFileDescriptor(
    ftl::UniqueFD source,
    mojo::ScopedDataPipeProducerHandle destination,
    ftl::TaskRunner* task_runner,
    const std::function<void(bool /*success*/)>& callback);

}  // namespace mtl

#endif  // LIB_MTL_DATA_PIPE_FILES_H_
