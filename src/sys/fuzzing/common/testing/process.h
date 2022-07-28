// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_PROCESS_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_PROCESS_H_

#include <lib/zx/channel.h>
#include <lib/zx/process.h>

#include <string>
#include <vector>

#include "src/lib/pkg_url/fuchsia_pkg_url.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Returns a process via |out| that has been started from the given executable |path|, and passes it
// the |url| of the fake component it belongs to as well as zero or more |channels| to services.
// Either the executable or the services may be real or test fakes, depending on which interactions
// are being tested.
__WARN_UNUSED_RESULT zx_status_t StartProcess(const std::string& path,
                                              const component::FuchsiaPkgUrl& url,
                                              std::vector<zx::channel> channels, zx::process* out);

// Promises to wait for the previously |Start|ed |process| to terminate using the given |executor|.
ZxPromise<> AwaitTermination(zx::process process, ExecutorPtr executor);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_PROCESS_H_
