// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <iostream>
#include <sstream>

#include <lib/async-loop/cpp/loop.h>
#include <launchpad/launchpad.h>
#include <test_runner/cpp/fidl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/object.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/time/stopwatch.h"

class Reporter {
 public:
  Reporter(async::Loop* loop,
           const std::string& name, test_runner::TestRunner* test_runner)
      : loop_(loop), name_(name), test_runner_(test_runner) {}

  ~Reporter() {}

  void Start() {
    test_runner_->Identify(name_, [] {});
    stopwatch_.Start();
  }

  void Finish(bool failed, const std::string& message) {
    test_runner::TestResult result;
    result.name = name_;
    result.elapsed = stopwatch_.Elapsed().ToMilliseconds();
    result.failed = failed;
    result.message = message;

    test_runner_->ReportResult(std::move(result));
    test_runner_->Teardown([this] { loop_->Quit(); });
    loop_->Run();
  }

 private:
  async::Loop* const loop_;
  std::string name_;
  test_runner::TestRunner* test_runner_;
  fxl::Stopwatch stopwatch_;
};

void ReadPipe(int pipe, std::stringstream* stream) {
  char buffer[1024];
  int size;
  while ((size = read(pipe, buffer, 1024))) {
    stream->write(buffer, size);
    std::cout.write(buffer, size);
  }
}

// Runs a command specified by argv, and based on its exit code reports success
// or failure to the TestRunner FIDL service.
int main(int argc, char** argv) {
  std::string name;
  bool command_provided;
  if (argc > 1) {
    command_provided = true;
    name = argv[1];
  } else {
    command_provided = false;
    name = "report_result";
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  auto test_runner =
      app_context->ConnectToEnvironmentService<test_runner::TestRunner>();
  Reporter reporter(&loop, name, test_runner.get());

  if (!command_provided) {
    reporter.Start();
    reporter.Finish(true, "No command provided");
    return 1;
  }

  launchpad_t* launchpad;
  int stdout_pipe;
  int stderr_pipe;
  launchpad_create(0, argv[1], &launchpad);
  launchpad_load_from_file(launchpad, argv[1]);
  launchpad_clone(launchpad, LP_CLONE_FDIO_NAMESPACE);
  launchpad_add_pipe(launchpad, &stdout_pipe, 1);
  launchpad_add_pipe(launchpad, &stderr_pipe, 2);
  launchpad_set_args(launchpad, argc - 1, argv + 1);

  reporter.Start();

  const char* error;
  zx_handle_t handle;
  zx_status_t status = launchpad_go(launchpad, &handle, &error);
  if (status < 0) {
    reporter.Finish(true, error);
    return 1;
  }

  std::stringstream stream;
  stream << "[stdout]\n";
  ReadPipe(stdout_pipe, &stream);
  stream << "[stderr]\n";
  ReadPipe(stderr_pipe, &stream);

  status =
      zx_object_wait_one(handle, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
  if (status != ZX_OK) {
    reporter.Finish(true, "Failed to wait for exit");
    return 1;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(handle, ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), NULL, NULL);
  zx_handle_close(handle);
  if (status < 0) {
    reporter.Finish(true, "Failed to get return code");
    return 1;
  }

  reporter.Finish(proc_info.return_code, stream.str());
  return 0;
}
