// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_main.h"

#include <cstdlib>
#include <filesystem>

#include <cmdline/args_parser.h>

#include "src/developer/debug/shared/buffered_fd.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_poll.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/actions.h"
#include "src/developer/debug/zxdb/console/command_line_options.h"
#include "src/developer/debug/zxdb/console/console_impl.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Loads any actions specified on the command line into the vector.
Err SetupActions(const CommandLineOptions& options, std::vector<Action>* actions) {
  if (options.core) {
    if (options.connect || options.run) {
      return Err("--core can't be used with commands to connect or run.");
    }

    std::string cmd = VerbToString(Verb::kOpenDump) + " " + *options.core;
    actions->push_back(Action("Open Dump", [cmd](const Action&, const Session&, Console* console) {
      console->ProcessInputLine(cmd.c_str(), ActionFlow::PostActionCallback);
    }));
  }

  if (options.connect) {
    std::string cmd = "connect " + *options.connect;
    actions->push_back(Action("Connect", [cmd](const Action&, const Session&, Console* console) {
      console->ProcessInputLine(cmd.c_str(), ActionFlow::PostActionCallback);
    }));
  }

  if (options.run) {
    std::string cmd = "run " + *options.run;
    actions->push_back(Action("Run", [cmd](const Action&, const Session&, Console* console) {
      console->ProcessInputLine(cmd.c_str(), ActionFlow::PostActionCallback);
    }));
  }

  if (options.script_file) {
    Err err = ScriptFileToActions(*options.script_file, actions);
    if (err.has_error())
      return err;
  }

  for (const auto& filter : options.filter) {
    std::string cmd = "filter attach " + filter;
    actions->push_back(Action("Filter", [cmd](const Action&, const Session&, Console* console) {
      console->ProcessInputLine(cmd.c_str(), ActionFlow::PostActionCallback);
    }));
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

void ScheduleActions(zxdb::Session& session, zxdb::Console& console,
                     std::vector<zxdb::Action> actions) {
  auto callback = [&](zxdb::Err err) {
    std::string msg;
    if (!err.has_error()) {
      msg = "All actions were executed successfully.";
    } else if (err.type() == zxdb::ErrType::kCanceled) {
      msg = "Action processing was cancelled.";
    } else {
      msg = fxl::StringPrintf("Error executing actions: %s", err.msg().c_str());
    }
    // Go into interactive mode.
    InitConsole(console);
  };

  // This will add the actions to the MessageLoop and oversee that all the actions run or the flow
  // is interrupted if one of them fails. Actions run on a singleton ActionFlow instance.
  zxdb::ActionFlow& flow = zxdb::ActionFlow::Singleton();
  flow.ScheduleActions(std::move(actions), &session, &console, callback);
}

void SetupCommandLineOptions(const CommandLineOptions& options, Session* session) {
  // Symbol paths
  std::vector<std::string> paths;
  // At this moment, the build index has all the "default" paths.
  BuildIDIndex& build_id_index = session->system().GetSymbols()->build_id_index();
  for (const auto& build_id_file : build_id_index.build_id_files())
    paths.push_back(build_id_file);
  for (const auto& source : build_id_index.sources())
    paths.push_back(source);

  // We add the options paths given paths.
  paths.insert(paths.end(), options.symbol_paths.begin(), options.symbol_paths.end());

  if (options.symbol_cache_path) {
    session->system().settings().SetString(ClientSettings::System::kSymbolCache,
                                           *options.symbol_cache_path);
  }

  if (!options.symbol_servers.empty()) {
    session->system().settings().SetList(ClientSettings::System::kSymbolServers,
                                         options.symbol_servers);
  }

  // Adding it to the settings will trigger the loading of the symbols. Redundant adds are ignored.
  session->system().settings().SetList(ClientSettings::System::kSymbolPaths, std::move(paths));

  session->system().settings().SetList(ClientSettings::System::kSymbolRepoPaths,
                                       options.symbol_repo_paths);

  session->system().settings().SetList(ClientSettings::Target::kBuildDirs, options.build_dirs);
}

}  // namespace

int ConsoleMain(int argc, const char* argv[]) {
  CommandLineOptions options;
  std::vector<std::string> params;
  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    fprintf(stderr, "%s", status.error_message().c_str());
    return 1;
  }

  std::vector<zxdb::Action> actions;
  Err err = SetupActions(options, &actions);
  if (err.has_error()) {
    fprintf(stderr, "%s\n", err.msg().c_str());
    return 1;
  }

  debug_ipc::MessageLoopPoll loop;
  std::string error_message;
  if (!loop.Init(&error_message)) {
    fprintf(stderr, "%s", error_message.c_str());
    return 1;
  }

  // This scope forces all the objects to be destroyed before the Cleanup() call which will mark the
  // message loop as not-current.
  {
    debug_ipc::BufferedFD buffer;

    // Route data from buffer -> session.
    Session session;
    buffer.set_data_available_callback([&session]() { session.OnStreamReadable(); });

    // TODO(donosoc): Do correct category setup.
    debug_ipc::SetLogCategories({debug_ipc::LogCategory::kAll});
    if (options.debug_mode) {
      debug_ipc::SetDebugMode(true);
      session.system().settings().SetBool(ClientSettings::System::kDebugMode, true);
    }

    ConsoleImpl console(&session);
    if (options.quit_agent_on_quit) {
      session.system().settings().SetBool(ClientSettings::System::kQuitAgentOnExit, true);
    }

    SetupCommandLineOptions(options, &session);

    if (!actions.empty()) {
      ScheduleActions(session, console, std::move(actions));
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
