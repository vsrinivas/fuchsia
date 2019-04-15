// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline/args_parser.h>
#include <signal.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "tools/fidlcat/lib/interception_workflow.h"

// TODO: Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "lib/fidl/cpp/message.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_parser.h"
#include "tools/fidlcat/lib/zx_channel_params.h"

namespace fidlcat {

struct CommandLineOptions {
  std::optional<std::string> connect;
  std::optional<std::string> remote_pid;
  std::vector<std::string> symbol_paths;
  std::vector<std::string> fidl_ir_paths;
};

const char kHelpIntro[] = R"(fidlcat [ <options> ] [ command [args] ]

  fidlcat will run the specified command until it exits.  It will intercept and
  record all fidl calls invoked by the process.  The command may be of the form
  "run <component URL>", in which case the given component will be launched.

Options:

)";

const char kRemoteHostHelp[] = R"(--connect
      The host and port of the target Fuchsia instance, of the form
      [<ipv6_addr>]:port.)";

const char kRemotePidHelp[] = R"(--remote-pid
      The koid of the remote process.)";

const char kFidlIrPathHelp[] = R"(--fidl-ir-path=<path>
      Adds the given path as a repository for FIDL IR, in the form of .fidl.json
      files.  Passing a file adds the given file.  Passing a directory adds all
      of the .fidl.json files in that directory and any directory transitively
      reachable from there. This switch can be passed multiple times to add
      multiple locations.)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

const char kSymbolPathHelp[] = R"(  --symbol-path=<path>
  -s <path>
      Adds the given directory or file to the symbol search path. Multiple
      -s switches can be passed to add multiple locations. When a directory
      path is passed, the directory will be enumerated non-recursively to
      index all ELF files, unless the directory contains a .build-id
      subdirectory, in which case that directory is assumed to contain an index
      of all ELF files within. When a .txt file is passed, it will be treated
      as a mapping database from build ID to file path. Otherwise, the path
      will be loaded as an ELF file (if possible).)";

cmdline::Status ParseCommandLine(int argc, const char* argv[],
                                 CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("connect", 'r', kRemoteHostHelp,
                   &CommandLineOptions::connect);
  parser.AddSwitch("remote-pid", 'p', kRemotePidHelp,
                   &CommandLineOptions::remote_pid);
  parser.AddSwitch("fidl-ir-path", 0, kFidlIrPathHelp,
                   &CommandLineOptions::fidl_ir_paths);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp,
                   &CommandLineOptions::symbol_paths);
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp,
                          [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  if (requested_help) {
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());
  }

  return cmdline::Status::Ok();
}

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
    // Maybe detach here.
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

// The meat of the program: decode the given zx_channel_write params.
void OnZxChannelWrite(LibraryLoader* loader, const zxdb::Err& err,
                      const ZxChannelWriteParams& params) {
  if (!err.ok()) {
    FXL_LOG(INFO) << "Unable to decode zx_channel_write params: " << err.msg();
    return;
  }
  fidl::BytePart bytes(params.GetBytes().get(), params.GetNumBytes(),
                       params.GetNumBytes());
  fidl::HandlePart handles(params.GetHandles().get(), params.GetNumHandles(),
                           params.GetNumHandles());
  fidl::Message message(std::move(bytes), std::move(handles));
  fidl_message_header_t header = message.header();
  const fidlcat::InterfaceMethod* method;
  if (!loader->GetByOrdinal(header.ordinal, &method)) {
    // Probably should print out raw bytes here instead.
    FXL_LOG(WARNING) << "Protocol method with ordinal " << header.ordinal
                     << " not found";
    return;
  }
  rapidjson::Document actual;
  if (!fidlcat::RequestToJSON(method, message, actual)) {
    FXL_LOG(WARNING) << "Could not parse data with type " << method->name()
                     << ", best effort displayed";
  }

  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  actual.Accept(writer);

#if 0
  fprintf(stderr, "ordinal = %d\n", header.ordinal);
  fprintf(stderr, "Output: %s\n", output.GetString());
#endif
  fprintf(stdout, "%s.%s = %s\n", method->enclosing_interface().name().c_str(),
          method->name().c_str(), output.GetString());
}

// Add the startup actions to the loop: connect, attach to pid, set breakpoints.
void EnqueueStartup(InterceptionWorkflow& workflow, LibraryLoader& loader,
                    CommandLineOptions& options,
                    std::vector<std::string>& params) {
  workflow.SetZxChannelWriteCallback(std::bind(
      OnZxChannelWrite, &loader, std::placeholders::_1, std::placeholders::_2));
  uint64_t process_koid = ULLONG_MAX;
  if (options.remote_pid) {
    std::string& pid_str = *options.remote_pid;
    process_koid = strtoull(pid_str.c_str(), nullptr, 10);
    // There is no process 0, and if there were, we probably wouldn't be able to
    // talk with it.
    if (process_koid == 0) {
      fprintf(stderr, "Invalid pid %s\n", pid_str.c_str());
      exit(1);
    }
  }

  std::string host;
  uint16_t port;
  zxdb::Err parse_err = zxdb::ParseHostPort(*(options.connect), &host, &port);
  if (!parse_err.ok()) {
    FXL_LOG(FATAL) << "Could not parse host/port pair: " << parse_err.msg();
  }

  auto set_breakpoints = [&workflow, process_koid](const zxdb::Err& err) {
    workflow.SetBreakpoints(process_koid);
  };

  auto attach = [&workflow, process_koid, params,
                 set_breakpoints =
                     std::move(set_breakpoints)](const zxdb::Err& err) {
    if (!err.ok()) {
      FXL_LOG(FATAL) << "Unable to connect: " << err.msg();
    }
    FXL_LOG(INFO) << "Connected!";
    if (process_koid != ULLONG_MAX) {
      workflow.Attach(process_koid, set_breakpoints);
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

namespace {

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

}  // namespace

// Gets the list of .fidl.json files from the command line flags.
//
// For each element in |cli_ir_paths|, add all transitively reachable .fidl.json
// files, and store them in |paths|.
void ExpandFidlPathsFromOptions(
    const std::vector<std::string>& cli_ir_paths,
    std::vector<std::unique_ptr<std::istream>>& paths) {
  // Maybe also get from a single file, per
  // https://fuchsia-review.googlesource.com/c/fuchsia/+/253357
  std::vector<std::string> workqueue = cli_ir_paths;
  std::set<std::string> checked_dirs;
  // Repeat until workqueue is empty:
  //  If it is a directory, add the directory contents to the workqueue.
  //  If it is a .fidl.json file, add it to |paths|.
  while (!workqueue.empty()) {
    std::string current_string = workqueue.back();
    workqueue.pop_back();
    std::filesystem::path current_path = current_string;
    if (std::filesystem::is_directory(current_path)) {
      for (auto& dir_ent : std::filesystem::directory_iterator(current_path)) {
        std::string ent_name = dir_ent.path().string();
        if (std::filesystem::is_directory(ent_name)) {
          auto found = checked_dirs.find(ent_name);
          if (found == checked_dirs.end()) {
            checked_dirs.insert(ent_name);
            workqueue.push_back(ent_name);
          }
        } else if (EndsWith(ent_name, ".fidl.json")) {
          paths.push_back(std::make_unique<std::ifstream>(dir_ent.path()));
        }
      }
    } else if (std::filesystem::is_regular_file(current_path) &&
               EndsWith(current_string, ".fidl.json")) {
      paths.push_back(std::make_unique<std::ifstream>(current_string));
    }
  }
}

int ConsoleMain(int argc, const char* argv[]) {
  CommandLineOptions options;
  std::vector<std::string> params;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    fprintf(stderr, "%s\n", status.error_message().c_str());
    return 1;
  }

  InterceptionWorkflow workflow;
  workflow.Initialize(options.symbol_paths);

  std::vector<std::unique_ptr<std::istream>> paths;
  ExpandFidlPathsFromOptions(options.fidl_ir_paths, paths);

  fidlcat::LibraryReadError loader_err;
  LibraryLoader loader(paths, &loader_err);
  if (loader_err.value != fidlcat::LibraryReadError::kOk) {
    FXL_LOG(ERROR) << "Failed to read libraries";
    return 1;
  }

  EnqueueStartup(workflow, loader, options, params);

  // TODO: When the attached koid terminates normally, we should exit and call
  // QuitNow() on the MessageLoop.
  workflow_.store(&workflow);
  CatchSigterm();

  workflow.Go();
  return 0;
}

}  // namespace fidlcat

int main(int argc, const char* argv[]) { fidlcat::ConsoleMain(argc, argv); }
