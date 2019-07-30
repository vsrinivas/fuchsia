// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "tools/fidlcat/command_line_options.h"
#include "tools/fidlcat/lib/interception_workflow.h"

// TODO: Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "lib/fidl/cpp/message.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/message_decoder.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

static bool called_onexit_once_ = false;
static std::atomic<InterceptionWorkflow*> workflow_;

static void OnExit(int signum, siginfo_t* info, void* ptr) {
  if (called_onexit_once_) {
    // Exit immediately.
#if defined(__APPLE__)
    _Exit(1);
#else
    _exit(1);
#endif
  } else {
    // Maybe detach cleanly here, if we can.
    FXL_LOG(INFO) << "Shutting down...";
    called_onexit_once_ = true;
    workflow_.load()->Shutdown();
  }
}

void CatchSigterm() {
  static struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_sigaction = OnExit;
  action.sa_flags = SA_SIGINFO;

  sigaction(SIGINT, &action, NULL);
}

// Add the startup actions to the loop: connect, attach to pid, set breakpoints.
void EnqueueStartup(InterceptionWorkflow& workflow, const CommandLineOptions& options,
                    std::vector<std::string>& params) {
  std::vector<uint64_t> process_koids;
  if (!options.remote_pid.empty()) {
    for (const std::string& pid_str : options.remote_pid) {
      uint64_t process_koid = strtoull(pid_str.c_str(), nullptr, 10);
      // There is no process 0, and if there were, we probably wouldn't be able to
      // talk with it.
      if (process_koid == 0) {
        fprintf(stderr, "Invalid pid %s\n", pid_str.c_str());
        exit(1);
      }
      process_koids.push_back(process_koid);
    }
  }

  std::string host;
  uint16_t port;
  zxdb::Err parse_err = zxdb::ParseHostPort(*(options.connect), &host, &port);
  if (!parse_err.ok()) {
    FXL_LOG(FATAL) << "Could not parse host/port pair: " << parse_err.msg();
  }

  auto set_breakpoints = [&workflow](const zxdb::Err& err, uint64_t process_koid) {
    workflow.SetBreakpoints(process_koid);
  };

  auto attach = [&workflow, process_koids, remote_name = options.remote_name, params,
                 set_breakpoints = std::move(set_breakpoints)](const zxdb::Err& err) {
    if (!err.ok()) {
      FXL_LOG(FATAL) << "Unable to connect: " << err.msg();
      return;
    }
    FXL_LOG(INFO) << "Connected!";
    if (!process_koids.empty()) {
      workflow.Attach(process_koids, set_breakpoints);
    } else if (!remote_name.empty()) {
      workflow.Filter(remote_name, set_breakpoints);
    } else {
      workflow.Launch(params, set_breakpoints);
    }
  };

  auto connect = [&workflow, attach = std::move(attach), host, port]() {
    FXL_LOG(INFO) << "Connecting to port " << port << " on " << host << "...";
    workflow.Connect(host, port, attach);
  };
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, connect);
}

int ConsoleMain(int argc, const char* argv[]) {
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  cmdline::Status status =
      ParseCommandLine(argc, argv, &options, &decode_options, &display_options, &params);
  if (status.has_error()) {
    fprintf(stderr, "%s\n", status.error_message().c_str());
    return 1;
  }

  std::vector<std::unique_ptr<std::istream>> paths;
  std::vector<std::string> bad_paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths, bad_paths);
  if (paths.size() == 0) {
    std::string error = "No FIDL IR paths provided.";
    if (bad_paths.size() != 0) {
      error.append(" File(s) not found: [ ");
      for (auto& s : bad_paths) {
        error.append(s);
        error.append(" ");
      }
      error.append("]");
    }
    FXL_LOG(INFO) << error;
  }

  fidlcat::LibraryReadError loader_err;
  LibraryLoader loader(paths, &loader_err);
  if (loader_err.value != fidlcat::LibraryReadError::kOk) {
    FXL_LOG(ERROR) << "Failed to read libraries";
    return 1;
  }

  InterceptionWorkflow workflow;
  workflow.Initialize(options.symbol_paths,
                      std::make_unique<SyscallDisplayDispatcher>(&loader, decode_options,
                                                                 display_options, std::cout));

  EnqueueStartup(workflow, options, params);

  // TODO: When the attached koid terminates normally, we should exit and call
  // QuitNow() on the MessageLoop.
  workflow_.store(&workflow);
  CatchSigterm();

  workflow.Go();
  return 0;
}

}  // namespace fidlcat

int main(int argc, const char* argv[]) { fidlcat::ConsoleMain(argc, argv); }
