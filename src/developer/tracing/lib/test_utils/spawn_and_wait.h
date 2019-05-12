// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_TRACE_TEST_UTILS_RUN_TEST_H_
#define GARNET_LIB_TRACE_TEST_UTILS_RUN_TEST_H_

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

// If |arg_handle| is not ZX_HANDLE_INVALID, then it is passed to the
// process with id PA_USER0.
zx_status_t SpawnProgram(const zx::job& job,
                         const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process);

zx_status_t WaitAndGetExitCode(const std::string& program_name,
                               const zx::process& process, int* out_exit_code);

#endif  // GARNET_LIB_TRACE_TEST_UTILS_RUN_TEST_H_
