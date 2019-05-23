// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <stdlib.h>

#include <fstream>
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
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_object.h"
#include "tools/fidlcat/lib/wire_parser.h"
#include "tools/fidlcat/lib/zx_channel_params.h"

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

std::string DocumentToString(rapidjson::Document& document) {
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  document.Accept(writer);
  return output.GetString();
}

void OnZxChannelAction(LibraryLoader* loader, const CommandLineOptions& options,
                       const zxdb::Err& err, const ZxChannelParams& params) {
  fidl::BytePart bytes(params.GetBytes().get(), params.GetNumBytes(),
                       params.GetNumBytes());
  fidl::HandlePart handles(params.GetHandles().get(), params.GetNumHandles(),
                           params.GetNumHandles());
  fidl::Message message(std::move(bytes), std::move(handles));
  if (message.payload().data() == nullptr) {
    FXL_LOG(WARNING) << "Message to be decoded contains no data.";
    return;
  }
  fidl_message_header_t header = message.header();
  const fidlcat::InterfaceMethod* method;
  if (!loader->GetByOrdinal(header.ordinal, &method)) {
    // Probably should print out raw bytes here instead.
    FXL_LOG(WARNING) << "Protocol method with ordinal " << header.ordinal
                     << " not found";
    return;
  }

  std::unique_ptr<fidlcat::Object> decoded_request;
  int matched_request_count =
      fidlcat::DecodeRequest(method, message, &decoded_request) ? 1 : 0;

  std::unique_ptr<fidlcat::Object> decoded_response;
  int matched_response_count =
      fidlcat::DecodeResponse(method, message, &decoded_response) ? 1 : 0;

  rapidjson::Document actual_request;
  rapidjson::Document actual_response;
  if (!options.pretty_print) {
    if (decoded_request != nullptr) {
      decoded_request->ExtractJson(actual_request.GetAllocator(),
                                   actual_request);
    }

    if (decoded_response != nullptr) {
      decoded_response->ExtractJson(actual_response.GetAllocator(),
                                    actual_response);
    }
  }

  const Colors& colors = options.needs_colors ? WithColors : WithoutColors;

#if 0
  fprintf(stderr, "ordinal = %d\n", header.ordinal);
#endif

  std::cout << colors.white_on_magenta << method->enclosing_interface().name()
            << '.' << method->name() << colors.reset << " = ";

  int size =
      method->enclosing_interface().name().size() + method->name().size() + 4;

  if (matched_request_count + matched_response_count == 1) {
    if (matched_request_count > 0) {
      if (options.pretty_print) {
        std::cout << colors.green << "request" << colors.reset << ' ';
        size += 8;
        decoded_request->PrettyPrint(std::cout, colors, 1,
                                     options.columns - size, options.columns);
      } else {
        std::cout << "(request):\n    " << DocumentToString(actual_request);
      }
    } else {
      if (options.pretty_print) {
        std::cout << colors.green << "response" << colors.reset << ' ';
        size += 9;
        decoded_response->PrettyPrint(std::cout, colors, 1,
                                      options.columns - size, options.columns);
      } else {
        std::cout << "(response):\n    " << DocumentToString(actual_response);
      }
    }
  } else {
    if (matched_request_count + matched_response_count == 0) {
      FXL_LOG(WARNING) << "Could not parse data with type " << method->name()
                       << ", best effort displayed";
    }
    // TODO(DX-1307): We can track whether this process has historically been
    // sending requests or responses on this channel, and surface based on that.
    // We may be able to indicate directionality in the message itself (e.g.,
    // with a bit in the txid).  Or do something else that's smarter than print
    // both out.
    if (options.pretty_print) {
      std::cout << colors.green << "request" << colors.reset << ' ';
      decoded_request->PrettyPrint(std::cout, colors, 1, 0, options.columns);
      std::cout << " or " << colors.green << "response" << colors.reset << ' ';
      decoded_response->PrettyPrint(std::cout, colors, 1, 0, options.columns);
    } else {
      std::cout << "One of (request):\n    " << DocumentToString(actual_request)
                << "\nor (response):\n    "
                << DocumentToString(actual_response);
    }
  }
  std::cout << '\n';
}

// Add the startup actions to the loop: connect, attach to pid, set breakpoints.
void EnqueueStartup(InterceptionWorkflow& workflow, LibraryLoader& loader,
                    const CommandLineOptions& options,
                    std::vector<std::string>& params) {
  workflow.SetZxChannelWriteCallback(
      [loader = &loader, &options](const zxdb::Err& err,
                                   const ZxChannelParams& params) {
        if (!err.ok()) {
          FXL_LOG(INFO) << "Unable to decode zx_channel_write params: "
                        << err.msg();
          return;
        }
        OnZxChannelAction(loader, options, err, params);
      });
  workflow.SetZxChannelReadCallback([loader = &loader, &options](
                                        const zxdb::Err& err,
                                        const ZxChannelParams& params) {
    if (!err.ok()) {
      FXL_LOG(INFO) << "Unable to decode zx_channel_read params: " << err.msg();
      return;
    }
    OnZxChannelAction(loader, options, err, params);
  });

  uint64_t process_koid = ULLONG_MAX;
  if (options.remote_pid) {
    const std::string& pid_str = *options.remote_pid;
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
      return;
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
