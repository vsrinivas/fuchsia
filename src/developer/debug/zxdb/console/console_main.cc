// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_main.h"

#include <lib/cmdline/args_parser.h>
#include <lib/fit/defer.h>

#include <cstdlib>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_poll.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/curl.h"
#include "src/developer/debug/zxdb/common/version.h"
#include "src/developer/debug/zxdb/console/analytics.h"
#include "src/developer/debug/zxdb/console/command_line_options.h"
#include "src/developer/debug/zxdb/console/command_sequence.h"
#include "src/developer/debug/zxdb/console/console_impl.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/debug_adapter/server.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Loads any actions specified on the command line into the vector.
Err SetupActions(const CommandLineOptions& options, std::vector<std::string>* actions) {
  if (options.core) {
    if (options.connect || options.run) {
      return Err("--core can't be used with commands to connect or run.");
    }
    actions->push_back(VerbToString(Verb::kOpenDump) + " " + *options.core);
  }

  if (options.connect)
    actions->push_back(VerbToString(Verb::kConnect) + " " + *options.connect);

  if (options.unix_connect)
    actions->push_back(VerbToString(Verb::kConnect) + " -u " + *options.unix_connect);

  if (options.run)
    actions->push_back(VerbToString(Verb::kRun) + " " + *options.run);

  if (options.script_file) {
    ErrOr<std::vector<std::string>> cmds_or = ReadCommandsFromFile(*options.script_file);
    if (cmds_or.has_error())
      return cmds_or.err();
    actions->insert(actions->end(), cmds_or.value().begin(), cmds_or.value().end());
  }

  for (const auto& attach : options.attach) {
    actions->push_back(VerbToString(Verb::kAttach) + " " + attach);
  }

  return Err();
}

void InitConsole(zxdb::Console& console) {
  console.Init();

  // Help text.
  OutputBuffer help;
  help.Append(Syntax::kWarning, "👉 ");
  help.Append(Syntax::kComment, "To get started, try \"status\" or \"help\".");
  console.Output(help);
}

void SetupCommandLineOptions(const CommandLineOptions& options, Session* session) {
  auto& system_settings = session->system().settings();

  if (options.symbol_cache) {
    system_settings.SetString(ClientSettings::System::kSymbolCache, *options.symbol_cache);
  }

  if (!options.symbol_index_files.empty()) {
    system_settings.SetList(ClientSettings::System::kSymbolIndexFiles, options.symbol_index_files);
  }

  if (!options.symbol_servers.empty()) {
    system_settings.SetList(ClientSettings::System::kSymbolServers, options.symbol_servers);
  }

  if (!options.symbol_paths.empty()) {
    system_settings.SetList(ClientSettings::System::kSymbolPaths, options.symbol_paths);
  }

  if (!options.build_id_dirs.empty()) {
    system_settings.SetList(ClientSettings::System::kBuildIdDirs, options.build_id_dirs);
  }

  if (!options.ids_txts.empty()) {
    system_settings.SetList(ClientSettings::System::kIdsTxts, options.ids_txts);
  }

  if (!options.build_dirs.empty()) {
    system_settings.SetList(ClientSettings::Target::kBuildDirs, options.build_dirs);
  }
}

}  // namespace

int ConsoleMain(int argc, const char* argv[]) {
  using ::analytics::core_dev_tools::EarlyProcessAnalyticsOptions;

  Curl::GlobalInit();
  auto deferred_cleanup_curl = fit::defer(Curl::GlobalCleanup);
  auto deferred_cleanup_analytics = fit::defer(Analytics::CleanUp);
  CommandLineOptions options;
  std::vector<std::string> params;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    fprintf(stderr, "%s", status.error_message().c_str());
    return 1;
  }

  if (options.requested_version) {
    printf("Version: %s\n", kBuildVersion);
    return 0;
  }

  if (EarlyProcessAnalyticsOptions<Analytics>(options.analytics, options.analytics_show)) {
    return 0;
  }

  std::vector<std::string> actions;
  Err err = SetupActions(options, &actions);
  if (err.has_error()) {
    fprintf(stderr, "%s\n", err.msg().c_str());
    return 1;
  }

  debug::MessageLoopPoll loop;
  std::string error_message;
  if (!loop.Init(&error_message)) {
    fprintf(stderr, "%s", error_message.c_str());
    return 1;
  }

  // This scope forces all the objects to be destroyed before the Cleanup() call which will mark the
  // message loop as not-current.
  {
    Session session;

    Analytics::Init(session, options.analytics);
    Analytics::IfEnabledSendInvokeEvent(&session);

    debug::SetLogCategories({debug::LogCategory::kAll});
    if (options.debug_mode) {
      session.system().settings().SetBool(ClientSettings::System::kDebugMode, true);
    }

    if (options.no_auto_attach_limbo) {
      session.system().settings().SetBool(ClientSettings::System::kAutoAttachLimbo, false);
    }

    std::unique_ptr<DebugAdapterServer> debug_adapter;
    if (options.enable_debug_adapter) {
      int port = options.debug_adapter_port;
      debug_adapter = std::make_unique<DebugAdapterServer>(&session, port);
      err = debug_adapter->Init();
      if (err.has_error()) {
        fprintf(stderr, "Failed to initialize debug adapter: %s\n", err.msg().c_str());
        loop.Cleanup();
        return EXIT_FAILURE;
      }
    }

    ConsoleImpl console(&session);
    SetupCommandLineOptions(options, &session);

    if (!actions.empty()) {
      RunCommandSequence(&console, std::move(actions),
                         [&console](const Err& err) { InitConsole(console); });
    } else {
      // Interactive mode is the default mode.
      InitConsole(console);
    }

    loop.Run();
  }

  loop.Cleanup();

  return 0;
}

}  // namespace zxdb
