// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ASYNC_SOCKET_H_
#define SRC_SYS_FUZZING_COMMON_ASYNC_SOCKET_H_

#include "src/sys/fuzzing/common/artifact.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"

namespace fuzzing {

// Creates a promise to read data from the |fidl_input| or |fidl_artifact| received by a FIDL call
// into a corresponding |Input| or |Artifact|. These methods take ownership of their inputs to
// ensure they live as long as the returned promises.
//
// Example:
//   auto fidl_input = my_sync_ptr->MyFidlMethod();
//   Input input;
//   fpromise::run_single_threaded(socket.Read(std::move(fidl_input))
//     .and_then([&] (Input& received) { input = std::move(received); ... }));
//
ZxPromise<Input> AsyncSocketRead(const ExecutorPtr& executor, FidlInput&& fidl_input);
ZxPromise<Artifact> AsyncSocketRead(const ExecutorPtr& executor, FidlArtifact&& fidl_artifact);

// Schedules a task to write data from an |input| or |artifact| to a corresponding |FidlInput| or
// |FidlArtifact|, which is returned. These methods take ownership of their inputs to ensure they
// live as long as the scheduled promises.
//
// Example:
//   socket.Write(my_input.Duplicate(), [&] (FidlInput&& fidl_input) {
//     my_ptr->MyFidlMethod(std::move(fidl_input);
//   )});
//
FidlInput AsyncSocketWrite(const ExecutorPtr& executor, Input&& input);
FidlArtifact AsyncSocketWrite(const ExecutorPtr& executor, Artifact&& input);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ASYNC_SOCKET_H_
