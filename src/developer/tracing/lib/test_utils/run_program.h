// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_TRACING_LIB_TEST_UTILS_RUN_PROGRAM_H_
#define SRC_DEVELOPER_TRACING_LIB_TEST_UTILS_RUN_PROGRAM_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

namespace tracing {
namespace test {

// Append the current state of fxl::logging to |argv|.
// |prefix| is prepended to each argument.
// For example, if |prefix| is "--foo=" and verbosity is 2, then
// "--foo=--verbose=2" will be appended to |argv|.
void AppendLoggingArgs(std::vector<std::string>* argv, const char* prefix,
                       const syslog::LogSettings& log_settings);

// Wrapper around |fdio_spawn_etc()|.
// If |arg_handle| is not ZX_HANDLE_INVALID, then it is passed to the
// process with id PA_USER0.
zx_status_t SpawnProgram(const zx::job& job, const std::vector<std::string>& argv,
                         zx_handle_t arg_handle, zx::process* out_process);

// Wrapper around |fdio_spawn_etc()|.
zx_status_t RunProgram(const zx::job& job, const std::vector<std::string>& argv, size_t num_actions,
                       const fdio_spawn_action_t* actions, zx::process* out_process);

// Wait for |process| to exit.
// |program_name| is for logging purposes.
bool WaitAndGetReturnCode(const std::string& program_name, const zx::process& process,
                          int64_t* out_return_code);

// Wrapper on |RunProgram(),WaitAndGetReturnCode()|.
// The program must exit with a zero return code for success.
bool RunProgramAndWait(const zx::job& job, const std::vector<std::string>& argv, size_t num_actions,
                       const fdio_spawn_action_t* actions);

// Run an app within |context|.
// |app| is the component's URL.
bool RunComponent(sys::ComponentContext* context, const std::string& app,
                  const std::vector<std::string>& args,
                  std::unique_ptr<fuchsia::sys::FlatNamespace> flat_namespace,
                  fuchsia::sys::ComponentControllerPtr* component_controller);

// Wait for component |component_controller| to exit.
// |program_name| is for logging purposes.
bool WaitAndGetReturnCode(const std::string& program_name, async::Loop* loop,
                          fuchsia::sys::ComponentControllerPtr* component_controller,
                          int64_t* out_return_code);

// Wrapper on |RunComponent(),WaitAndGetReturnCode()|.
// The program must exit with a zero return code for success.
bool RunComponentAndWait(async::Loop* loop, sys::ComponentContext* context, const std::string& app,
                         const std::vector<std::string>& args,
                         std::unique_ptr<fuchsia::sys::FlatNamespace> flat_namespace);

}  // namespace test
}  // namespace tracing

#endif  // SRC_DEVELOPER_TRACING_LIB_TEST_UTILS_RUN_PROGRAM_H_
