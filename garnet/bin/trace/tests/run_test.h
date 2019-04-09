// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_RUN_TEST_H_
#define GARNET_BIN_TRACE_TESTS_RUN_TEST_H_

#include <lib/sys/cpp/component_context.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

// For now don't run longer than this. The CQ bot has this timeout as well,
// so this is as good a value as any. Later we might want to add a timeout
// value to tspecs.
constexpr zx_duration_t kTestTimeout = ZX_SEC(60);

void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix);

// If |arg_handle| is not ZX_HANDLE_INVALID, then it is passed to the
// process with id PA_USER0.
zx_status_t SpawnProgram(const zx::job& job,
                         const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process);

zx_status_t WaitAndGetExitCode(const std::string& program_name,
                               const zx::process& process, int* out_exit_code);

// We don't need to pass a context to RunTspec because the trace program
// is currently a system app. If that changes then we will need a context
// to run the trace too.
bool RunTspec(sys::ComponentContext* context,
              const std::string& relative_tspec_path,
              const std::string& output_file_path);

// N.B. This is a synchronous call that uses the default async dispatcher
// ("synchronous" meaning that it waits for the verifier to complete).
// Therefore the caller cannot currently be using it.
bool VerifyTspec(sys::ComponentContext* context,
                 const std::string& relative_tspec_path,
                 const std::string& output_file_path);

#endif  // GARNET_BIN_TRACE_TESTS_RUN_TEST_H_
