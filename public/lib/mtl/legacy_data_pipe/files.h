// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_LEGACY_DATA_PIPE_FILES_H_
#define LIB_MTL_LEGACY_DATA_PIPE_FILES_H_

#include <stdio.h>

#include <string>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace mtl {

// Asynchronously copies data from source to the destination file descriptor.
// The given |callback| is run upon completion. File writes and |callback| will
// be scheduled on the given |task_runner|.
void LegacyCopyToFileDescriptor(
    mojo::ScopedDataPipeConsumerHandle source,
    ftl::UniqueFD destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool /*success*/, ftl::UniqueFD /*destination*/)>&
        callback);

// Asynchronously copies data from source file to the destination. The given
// |callback| is run upon completion. File reads and |callback| will be
// scheduled to the given |task_runner|.
void LegacyCopyFromFileDescriptor(
    ftl::UniqueFD source,
    mojo::ScopedDataPipeProducerHandle destination,
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    const std::function<void(bool /*success*/, ftl::UniqueFD /*source*/)>&
        callback);

}  // namespace mtl

#endif  // LIB_MTL_LEGACY_DATA_PIPE_FILES_H_
