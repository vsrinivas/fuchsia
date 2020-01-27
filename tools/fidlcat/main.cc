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
#include "tools/fidlcat/lib/comparator.h"
#include "tools/fidlcat/lib/interception_workflow.h"

// TODO(fidlcat): Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "lib/fidl/cpp/message.h"
#include "src/developer/debug/zxdb/common/inet_util.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

static bool called_onexit_once_ = false;
static std::atomic<InterceptionWorkflow*> workflow_;

static void OnExit(int /*signum*/, siginfo_t* /*info*/, void* /*ptr*/) {
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

  sigaction(SIGINT, &action, nullptr);
}

// Add the startup actions to the loop: connect, attach to pid, set breakpoints.
void EnqueueStartup(InterceptionWorkflow* workflow, const CommandLineOptions& options,
                    const std::vector<std::string>& params) {
  std::vector<zx_koid_t> process_koids;
  if (!options.remote_pid.empty()) {
    for (const std::string& pid_str : options.remote_pid) {
      zx_koid_t process_koid = strtoull(pid_str.c_str(), nullptr, fidl_codec::kDecimalBase);
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

  auto attach = [workflow, process_koids, remote_name = options.remote_name,
                 params](const zxdb::Err& err) {
    if (!err.ok()) {
      FXL_LOG(FATAL) << "Unable to connect: " << err.msg();
      return;
    }
    FXL_LOG(INFO) << "Connected!";
    if (!process_koids.empty()) {
      workflow->Attach(process_koids);
    }
    if (remote_name.empty()) {
      if (std::find(params.begin(), params.end(), "run") != params.end()) {
        zxdb::Target* target = workflow->GetNewTarget();
        workflow->Launch(target, params);
      }
    } else {
      zxdb::Target* target = workflow->GetNewTarget();
      if (std::find(params.begin(), params.end(), "run") != params.end()) {
        workflow->Launch(target, params);
      }
      workflow->Filter(remote_name);
    }
  };

  auto connect = [workflow, attach = std::move(attach), host, port]() {
    FXL_LOG(INFO) << "Connecting to port " << port << " on " << host << "...";
    workflow->Connect(host, port, attach);
  };
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, connect);
}

int ConsoleMain(int argc, const char* argv[]) {
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  int remaining_servers = 0;
  bool server_error = false;
  std::string error =
      ParseCommandLine(argc, argv, &options, &decode_options, &display_options, &params);
  if (!error.empty()) {
    fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  std::vector<std::unique_ptr<std::istream>> paths;
  std::vector<std::string> bad_paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths, bad_paths);
  if (paths.empty()) {
    std::string error = "No FIDL IR paths provided.";
    if (!bad_paths.empty()) {
      error.append(" File(s) not found: [ ");
      for (auto& s : bad_paths) {
        error.append(s);
        error.append(" ");
      }
      error.append("]");
    }
    FXL_LOG(INFO) << error;
  }

  fidl_codec::LibraryReadError loader_err;
  fidl_codec::LibraryLoader loader(&paths, &loader_err);
  if (loader_err.value != fidl_codec::LibraryReadError::kOk) {
    FXL_LOG(ERROR) << "Failed to read libraries";
    return 1;
  }

  std::shared_ptr<Comparator> comparator =
      options.compare_file.has_value()
          ? std::make_shared<Comparator>(options.compare_file.value(), std::cout)
          : nullptr;

  std::unique_ptr<SyscallDecoderDispatcher> decoder_dispatcher =
      options.compare_file.has_value() ? std::make_unique<SyscallCompareDispatcher>(
                                             &loader, decode_options, display_options, comparator)
                                       : std::make_unique<SyscallDisplayDispatcher>(
                                             &loader, decode_options, display_options, std::cout);

  InterceptionWorkflow workflow;
  workflow.Initialize(options.symbol_paths, options.symbol_repo_paths, options.symbol_cache_path,
                      options.symbol_servers, std::move(decoder_dispatcher));

  if (workflow.HasSymbolServers()) {
    for (const auto& server : workflow.GetSymbolServers()) {
      // The first time we connect to a server, we have to provide an authentication.
      // After that, the key is cached.
      if (server->state() == zxdb::SymbolServer::State::kAuth) {
        std::string key;
        std::cout << "To authenticate " << server->name()
                  << ", please supply an authentication token. You can retrieve a token from:\n"
                  << server->AuthInfo() << '\n'
                  << "Enter the server authentication key: ";
        std::cin >> key;

        // Do the authentication.
        ++remaining_servers;
        server->Authenticate(key,
                             [&workflow, &remaining_servers, &server_error](const zxdb::Err& err) {
                               if (err.has_error()) {
                                 FXL_LOG(ERROR) << "Server authentication failed: " << err.msg();
                                 server_error = true;
                               }
                               if (--remaining_servers == 0) {
                                 if (server_error) {
                                   workflow.Shutdown();
                                 } else {
                                   FXL_LOG(INFO) << "Authentication successful";
                                 }
                               }
                             });
      }
      // We want to know when all the symbol servers are ready. We can only start
      //  monitoring when all the servers are ready.
      server->set_state_change_callback(
          [&workflow, &options, &params](zxdb::SymbolServer* server,
                                         zxdb::SymbolServer::State state) {
            if (state == zxdb::SymbolServer::State::kUnreachable) {
              server->set_state_change_callback(nullptr);
              FXL_LOG(ERROR) << "Can't connect to symbol server";
            } else if (state == zxdb::SymbolServer::State::kReady) {
              server->set_state_change_callback(nullptr);
              bool ready = true;
              for (const auto& server : workflow.GetSymbolServers()) {
                if (server->state() != zxdb::SymbolServer::State::kReady) {
                  ready = false;
                }
              }
              if (ready) {
                // Now all the symbol servers are ready. We can start fidlcat work.
                FXL_LOG(INFO) << "Connected to symbol server";
                EnqueueStartup(&workflow, options, params);
              }
            }
          });
    }
  } else {
    // No symbol server => directly start monitoring.
    EnqueueStartup(&workflow, options, params);
  }

  workflow_.store(&workflow);
  CatchSigterm();

  // Start waiting for events on the message loop.
  // When all the monitored process will be terminated, we will exit the loop.
  InterceptionWorkflow::Go();

  if (options.compare_file.has_value()) {
    comparator->FinishComparison();
  }

  return 0;
}

}  // namespace fidlcat

int main(int argc, const char* argv[]) { fidlcat::ConsoleMain(argc, argv); }
