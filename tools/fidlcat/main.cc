// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <signal.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "src/developer/debug/shared/curl.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/common/inet_util.h"
#include "src/developer/debug/zxdb/common/version.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "tools/fidlcat/command_line_options.h"
#include "tools/fidlcat/lib/analytics.h"
#include "tools/fidlcat/lib/comparator.h"
#include "tools/fidlcat/lib/interception_workflow.h"
#include "tools/fidlcat/lib/replay.h"
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
    FX_LOGS(INFO) << "Shutting down...";
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
  uint16_t port = 0;
  if (options.connect) {
    zxdb::Err parse_err = zxdb::ParseHostPort(*(options.connect), &host, &port);
    if (!parse_err.ok()) {
      fprintf(stderr, "Could not parse host/port pair: %s", parse_err.msg().c_str());
      exit(1);
    }
  }

  auto attach = [workflow, process_koids, &options, params](const zxdb::Err& err) {
    if (!err.ok()) {
      fprintf(stderr, "Unable to connect: %s", err.msg().c_str());
      exit(2);
    }
    FX_LOGS(INFO) << "Connected!";
    if (!process_koids.empty()) {
      workflow->Attach(process_koids);
    }
    if (options.remote_name.empty() && options.extra_name.empty()) {
      if (std::find(params.begin(), params.end(), "run") != params.end()) {
        zxdb::Target* target = workflow->GetNewTarget();
        workflow->Launch(target, params);
      }
    } else {
      zxdb::Target* target = workflow->GetNewTarget();
      if (std::find(params.begin(), params.end(), "run") != params.end()) {
        workflow->Launch(target, params);
      }
      if (options.remote_job_id.empty() && options.remote_job_name.empty()) {
        workflow->Filter(options.remote_name, /*main_filter=*/true, nullptr);
        workflow->Filter(options.extra_name, /*main_filter=*/false, nullptr);
      }
    }
    if (!options.remote_job_id.empty() || !options.remote_job_name.empty()) {
      workflow->session()->system().GetProcessTree(
          [workflow, &options](const zxdb::Err& err, debug_ipc::ProcessTreeReply reply) {
            workflow->AttachToJobs(reply.root, options.remote_job_id, options.remote_job_name,
                                   options.remote_name, options.extra_name);
          });
    }
  };

  auto connect = [workflow, options, attach = std::move(attach), host, port]() {
    if (options.connect) {
      FX_LOGS(INFO) << "Connecting to port " << port << " on " << host << "...";
      workflow->Connect(host, port, attach);
    } else {
      FX_LOGS(INFO) << "Connecting to " << *options.unix_connect << "...";
      workflow->UnixConnect(*options.unix_connect, attach);
    }
  };
  debug::MessageLoop::Current()->PostTask(FROM_HERE, connect);
}

int ConsoleMain(int argc, const char* argv[]) {
  using ::analytics::core_dev_tools::EarlyProcessAnalyticsOptions;

  debug::Curl::GlobalInit();
  auto deferred_cleanup_curl = fit::defer(debug::Curl::GlobalCleanup);
  auto deferred_cleanup_analytics = fit::defer(Analytics::CleanUp);
  CommandLineOptions options;
  DecodeOptions decode_options;
  DisplayOptions display_options;
  std::vector<std::string> params;
  std::string error =
      ParseCommandLine(argc, argv, &options, &decode_options, &display_options, &params);
  if (!error.empty()) {
    fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (options.requested_version) {
    printf("Version: %s\n", zxdb::kBuildVersion);
    return 0;
  }

  if (EarlyProcessAnalyticsOptions<Analytics>(options.analytics, options.analytics_show)) {
    return 0;
  }
  Analytics::InitBotAware(options.analytics);
  Analytics::IfEnabledSendInvokeEvent();

  std::vector<std::string> paths;
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
    FX_LOGS(INFO) << error;
  }

  fidl_codec::LibraryReadError loader_err;
  fidl_codec::LibraryLoader loader(paths, &loader_err);
  loader.ParseBuiltinSemantic();
  if (loader_err.value != fidl_codec::LibraryReadError::kOk) {
    FX_LOGS(ERROR) << "Failed to load FIDL definitions. Some FIDL messages may not be decoded.";
  }

  std::shared_ptr<Comparator> comparator =
      options.compare_file.has_value()
          ? std::make_shared<Comparator>(options.compare_file.value(), std::cout)
          : nullptr;

  std::unique_ptr<SyscallDisplayDispatcher> decoder_dispatcher =
      options.compare_file.has_value() ? std::make_unique<SyscallCompareDispatcher>(
                                             &loader, decode_options, display_options, comparator)
                                       : std::make_unique<SyscallDisplayDispatcher>(
                                             &loader, decode_options, display_options, std::cout);

  if (decode_options.input_mode == InputMode::kFile) {
    fidlcat::Replay replay(decoder_dispatcher.get());
    if (decode_options.output_mode == OutputMode::kTextProtobuf) {
      if (!replay.DumpProto(options.from)) {
        return 1;
      }
    } else {
      if (!replay.ReplayProto(options.from)) {
        return 1;
      }
      replay.dispatcher()->SessionEnded();
    }
  } else if (decode_options.input_mode == InputMode::kDump) {
    fidlcat::Replay replay(decoder_dispatcher.get());
    replay.DecodeTrace(std::cin);
    replay.dispatcher()->SessionEnded();
  } else {
    InterceptionWorkflow workflow;
    workflow.Initialize(options.symbol_index_files, options.symbol_paths, options.build_id_dirs,
                        options.ids_txts, options.symbol_cache, options.symbol_servers,
                        std::move(decoder_dispatcher), options.quit_agent_on_exit);

    if (workflow.HasSymbolServers()) {
      for (const auto& server : workflow.GetSymbolServers()) {
        // The first time we connect to a server, we have to provide an authentication.
        // After that, the key is cached.
        if (server->state() == zxdb::SymbolServer::State::kAuth) {
          workflow.AuthenticateServer(server);
        }
        // We want to know when all the symbol servers are ready. We can only start
        //  monitoring when all the servers are ready.
        server->set_state_change_callback(
            [&workflow, &options, &params](zxdb::SymbolServer* server,
                                           zxdb::SymbolServer::State state) {
              if (state == zxdb::SymbolServer::State::kAuth) {
                workflow.AuthenticateServer(server);
              } else if (state == zxdb::SymbolServer::State::kUnreachable) {
                server->set_state_change_callback(nullptr);
                FX_LOGS(ERROR) << "Can't connect to symbol server";
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
                  FX_LOGS(INFO) << "Connected to symbol server " << server->name();
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

    workflow.syscall_decoder_dispatcher()->SessionEnded();

    if (options.compare_file.has_value()) {
      comparator->FinishComparison();
    }
  }

  return 0;
}

}  // namespace fidlcat

int main(int argc, const char* argv[]) { fidlcat::ConsoleMain(argc, argv); }
