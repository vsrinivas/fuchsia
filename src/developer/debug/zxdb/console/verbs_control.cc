// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <algorithm>
#include <filesystem>

#include "lib/fit/defer.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/inet_util.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/status.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kExpressionsName[] = "expressions";
const char kExpressionsHelp[] = R"(Expressions

  Expressions appear in some commands, most notably "print":

    [zxdb] print &object->array_data[i + 4]
    (*)71cc72b5310

  Most C++ and Rust operators are implemented in a compatible way. Function
  calls are not currently supported (with exceptions, see "Pretty printers"
  below). Language-overloaded operators are ignored.

Variable and type names

  Names are evaluated in the current context according to C++ rules. This means
  that zxdb will search the current frame's local variables, function
  parameters, variables on "this" and its base-classes, variables in the current
  namespace and enclosing namespace.

  Type names are handled similarly, so type names used in casts need not specify
  namespaces or class names if the current frame is in that namespace or class.

  However, template parameters in type names must match exactly with the names
  in the symbol file. This includes all namespaces and, critically for C++ STL,
  all optional template parameters like allocator names.

  It is not currently possible to refer to types and statics defined locally to
  a function when the current scope is outside that function.

Casting

  The following casts are supported in a C++-compatible way:

    • (Foo*)0x1234567
    • reinterpret_cast<Foo*>(bar)
    • static_cast<int>(foo)

  Unlike in C++, const has no effect in the debugger so there is no const_cast.

  Rust expressions in zxdb should currently use C++ casts (bug 6001)

CPU registers

  Unambiguously refer to CPU registers using the form "$regname", so on x64
  "$rax" or "$xmm0". If there is no collision with named values in the debugged
  process, the bare register name can also be used, so "rax" and "xmm0".

  Vector registers are interpreted according to the current vector-format option
  (see "get vector-format" for possibilities, and "set vector-format <new_mode>"
  to set). They will be converted to arrays of the extracted values. Array
  notation can be used to refer to individual values. Using "double" vector
  format on a 128-bit ARM "v6" register would give:

    [zxdb] print $v6
    {0.0, 3.14}

    [zxdb] print $v6[1]
    3.14

    [zxdb] print $v6[0] = 2.71    # Assignment to a vector sub-value.
    2.71

  Importantly, since they are arrays, vector registers used in expressions print
  the 0th element first and increase to the right. This can be surprising
  because it's traditional to show vector registers with the high order bits on
  the left and indices decreasing to the right. Use the "regs" command for a
  vector-specific presentation if you want this format.

Pretty printers

  The debugger's pretty-printing system formats objects with complex internal
  definitions to be presented in a way that the user expects. This system also
  provides pretend data members, array access, and member functions for
  expressions so these objects behave as expected.

  The pretend functions are implemented internally in the debugger as
  expressions rather than executing any code in the debugged process. Only
  getters that take no arguments are currently supported.

  For example, vector- and string-like objects can be indexed with "[ <index> ]"
  and in C++ you can call back(), capacity(), empty(), front(), size(), and in
  Rust you can call as_ptr(), as_mut_ptr(), capacity(), is_empty(), len().

    [zxdb] print some_std_vector.size()
    5

    [zxdb] print some_std_vector[2]
    42

  Smart pointer, optional, and variant object can be dereferenced with "*" and
  "-> operators.

    [zxdb] print some_optional
    std::optional({x = 5, y = 1})

    [zxdb] print *some_optional
    {x = 5, y = 1}

    [zxdb] print some_optional->x
    5
)";

// help --------------------------------------------------------------------------------------------

const char kHelpShortHelp[] = R"(help / h: Help.)";
const char kHelpHelp[] =
    R"(help

  Yo dawg, I heard you like help on your help so I put help on the help in
  the help.)";

const char kHelpIntro[] =
    R"(
  Verbs
      "step"
          Applies the "step" verb to the currently selected thread.
      "mem-read --size=16 0x12345678"
          Pass a named switch and an argument.

  Nouns
      "thread"
          List available threads
      "thread 1"
          Select thread with ID 1 to be the default.

  Noun-Verb combinations
      "thread 4 step"
          Steps thread 4 of the current process regardless of the currently
          selected thread.
      "process 1 thread 4 step"
          Steps thread 4 of process 1 regardless of the currently selected
          thread or process.
)";

const char kOtherTopics[] =
    R"(
  expressions: Information on expressions used in "print", etc.

)";

// Format and syntax-highlights a line of the form "<name>: <description>". If there's no colon the
// line will be not syntax highlighted.
OutputBuffer FormatIndexLine(const std::string& line) {
  OutputBuffer help("  ");  // Indent.

  if (size_t colon_index = line.find(':'); colon_index != std::string::npos) {
    std::string name = line.substr(0, colon_index);

    // Some names have alternate forms, "foo / f". Don't highlight slashes as names so it's more
    // clear what things are the name.
    while (!name.empty()) {
      if (size_t slash = name.find('/'); slash != std::string::npos) {
        help.Append(Syntax::kVariable, name.substr(0, slash));
        help.Append(Syntax::kComment, "/");
        name = name.substr(slash + 1);
      } else {
        help.Append(Syntax::kVariable, name);
        break;
      }
    }

    help.Append(line.substr(colon_index));
  } else {
    // No syntax formatting for this line.
    help.Append(line);
  }
  help.Append("\n");
  return help;
}

OutputBuffer FormatGroupHelp(const char* heading, std::vector<std::string>* items) {
  std::sort(items->begin(), items->end());

  OutputBuffer help("\n");
  help.Append(Syntax::kHeading, heading);
  help.Append("\n");
  for (const auto& line : *items)
    help.Append(FormatIndexLine(line));
  return help;
}

OutputBuffer GetReference() {
  OutputBuffer help(Syntax::kHeading, "Help!");
  help.Append("\n\n  Type \"help <command>\" for command-specific help.\n\n");

  help.Append(Syntax::kHeading, "Other help topics");
  help.Append(" (see \"help <topic>\")\n");
  help.Append(kOtherTopics);

  help.Append(Syntax::kHeading, "Command syntax\n");

  help.Append(kHelpIntro);

  // Group all verbs by their CommandGroup. Add nouns to this since people will expect, for example,
  // "breakpoint" to be in the breakpoints section.
  std::map<CommandGroup, std::vector<std::string>> groups;

  // Get the separate noun reference and add to the groups.
  help.Append(Syntax::kHeading, "\nNouns\n");
  std::vector<std::string> noun_lines;
  for (const auto& pair : GetNouns()) {
    noun_lines.push_back(pair.second.short_help);
    groups[pair.second.command_group].push_back(pair.second.short_help);
  }
  std::sort(noun_lines.begin(), noun_lines.end());
  for (const auto& line : noun_lines)
    help.Append(FormatIndexLine(line));

  // Add in verbs.
  for (const auto& pair : GetVerbs())
    groups[pair.second.command_group].push_back(pair.second.short_help);

  help.Append(FormatGroupHelp("General", &groups[CommandGroup::kGeneral]));
  help.Append(FormatGroupHelp("Process", &groups[CommandGroup::kProcess]));
  help.Append(FormatGroupHelp("Symbol", &groups[CommandGroup::kSymbol]));
  help.Append(FormatGroupHelp("Assembly", &groups[CommandGroup::kAssembly]));
  help.Append(FormatGroupHelp("Breakpoint", &groups[CommandGroup::kBreakpoint]));
  help.Append(FormatGroupHelp("Query", &groups[CommandGroup::kQuery]));
  help.Append(FormatGroupHelp("Step", &groups[CommandGroup::kStep]));

  return help;
}

Err DoHelp(ConsoleContext* context, const Command& cmd) {
  OutputBuffer out;

  if (cmd.args().empty()) {
    // Generic help, list topics and quick reference.
    Console::get()->Output(GetReference());
    return Err();
  }
  const std::string& on_what = cmd.args()[0];

  const char* help = nullptr;

  // Check for a noun.
  const auto& string_noun = GetStringNounMap();
  auto found_string_noun = string_noun.find(on_what);
  if (found_string_noun != string_noun.end()) {
    // Find the noun record to get the help. This is guaranteed to exist.
    const auto& nouns = GetNouns();
    help = nouns.find(found_string_noun->second)->second.help;
  } else {
    // Check for a verb
    const auto& string_verb = GetStringVerbMap();
    auto found_string_verb = string_verb.find(on_what);
    if (found_string_verb != string_verb.end()) {
      // Find the verb record to get the help. This is guaranteed to exist.
      const auto& verbs = GetVerbs();
      help = verbs.find(found_string_verb->second)->second.help;
    } else {
      // Check for standalone topic.
      if (on_what == kExpressionsName) {
        help = kExpressionsHelp;
      } else {
        // Not a valid command.
        out.Append(Err("\"" + on_what +
                       "\" is not a valid command.\n"
                       "Try just \"help\" to get a list."));
        Console::get()->Output(out);
        return Err();
      }
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(out);
  return Err();
}

// quit --------------------------------------------------------------------------------------------

const char kQuitShortHelp[] = R"(quit / q / exit: Quits the debugger.)";
const char kQuitHelp[] =
    R"(quit

  Quits the debugger. It will prompt for confirmation if there are running
  processes.
)";

Err DoQuit(ConsoleContext* context, const Command& cmd) {
  int running_processes = 0;
  for (Target* t : context->session()->system().GetTargets()) {
    if (t->GetState() != Target::kNone)
      running_processes++;
  }

  if (running_processes == 0) {
    // Nothing running, quit immediately.
    Console::get()->Quit();
    return Err();
  }

  OutputBuffer message;
  if (running_processes == 1) {
    message =
        OutputBuffer("\nAre you sure you want to quit and detach from the running process?\n");
  } else {
    message = OutputBuffer(
        fxl::StringPrintf("\nAre you sure you want to quit and detach from %d running processes?\n",
                          running_processes));
  }

  line_input::ModalPromptOptions options;
  options.require_enter = false;
  options.case_sensitive = false;
  options.options.push_back("y");
  options.options.push_back("n");
  options.cancel_option = "n";
  Console::get()->ModalGetOption(options, message, "y/n > ", [](const std::string& answer) {
    if (answer == "y")
      Console::get()->Quit();
  });

  return Err();
}

// quit-agent --------------------------------------------------------------------------------------

const char kQuitAgentShortHelp[] = R"(quit-agent: Quits the debug agent.)";
const char kQuitAgentHelp[] =
    R"(quit-agent

  Quits the connected debug agent running on the target.)";

Err DoQuitAgent(ConsoleContext* context, const Command& cmd) {
  context->session()->QuitAgent([](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Successfully stopped the debug agent.");
    }
  });

  return Err();
}

// connect -----------------------------------------------------------------------------------------

const char kConnectShortHelp[] = R"(connect: Connect to a remote system for debugging.)";
const char kConnectHelp[] =
    R"(connect [ <remote_address> ]

  Connects to a debug_agent at the given address/port. With no arguments,
  attempts to reconnect to the previously used remote address.

  See also "disconnect".

Addresses

  Addresses can be of the form "<host> <port>" or "<host>:<port>". When using
  the latter form, IPv6 addresses must be [bracketed]. Otherwise the brackets
  are optional.

Examples

  connect mystem.localnetwork 1234
  connect mystem.localnetwork:1234
  connect 192.168.0.4:1234
  connect 192.168.0.4 1234
  connect [1234:5678::9abc] 1234
  connect 1234:5678::9abc 1234
  connect [1234:5678::9abc]:1234
)";

Err DoConnect(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  // Can accept either one or two arg forms.
  std::string host;
  uint16_t port = 0;

  // 0 args means pass empty string and 0 port to try to reconnect.
  if (cmd.args().size() == 1) {
    const std::string& host_port = cmd.args()[0];
    // Provide an additional assist to users if they forget to wrap an IPv6 address in [].
    if (Ipv6HostPortIsMissingBrackets(host_port)) {
      return Err(ErrType::kInput,
                 "For IPv6 addresses use either: \"[::1]:1234\"\n"
                 "or the two-parameter form: \"::1 1234.");
    }
    Err err = ParseHostPort(host_port, &host, &port);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() == 2) {
    Err err = ParseHostPort(cmd.args()[0], cmd.args()[1], &host, &port);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() > 2) {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->Connect(
      host, port, [callback = std::move(callback), cmd](const Err& err) mutable {
        if (err.has_error()) {
          // Don't display error message if they canceled the connection.
          if (err.type() != ErrType::kCanceled)
            Console::get()->Output(err);
        } else {
          OutputBuffer msg;
          msg.Append("Connected successfully.\n");

          // Assume if there's a callback this is not being run interactively. Otherwise, show the
          // usage tip.
          if (!callback) {
            msg.Append(Syntax::kWarning, "👉 ");
            msg.Append(Syntax::kComment,
                       "Normally you will \"run <program path>\" or \"attach "
                       "<process koid>\".");
          }
          Console::get()->Output(msg);
        }

        if (callback)
          callback(err);
      });
  Console::get()->Output("Connecting (use \"disconnect\" to cancel)...\n");

  return Err();
}

// opendump ----------------------------------------------------------------------------------------

const char kOpenDumpShortHelp[] = R"(opendump: Open a dump file for debugging.)";
const char kOpenDumpHelp[] =
    R"(opendump <path>

  Opens a dump file. Currently only the 'minidump' format is supported.

  With the dump open, you will be able to list processes and threads, view the
  memory map at the time the dump occurred, obtain a backtrace of threads, and
  read some memory from the time of the crash. What memory is readable depends
  on what the dump chose to include and what binaries are available from the
  original system.
)";

Err DoOpenDump(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  std::string path;

  if (cmd.args().size() == 0) {
    return Err(ErrType::kInput, "Need path to open.");
  } else if (cmd.args().size() == 1) {
    path = cmd.args()[0];
  } else {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->OpenMinidump(path, [callback = std::move(callback)](const Err& err) mutable {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Dump loaded successfully.\n");
    }

    if (callback)
      callback(err);
  });
  Console::get()->Output("Opening dump file...\n");

  return Err();
}

void DoCompleteOpenDump(const Command& cmd, const std::string& prefix,
                        std::vector<std::string>* completions) {
  if (!cmd.args().empty()) {
    return;
  }

  std::error_code ec;

  std::filesystem::path path;
  std::string filename;

  if (prefix.empty()) {
    path = std::filesystem::current_path(ec);

    if (ec) {
      return;
    }
  } else if (std::filesystem::exists(prefix, ec)) {
    if (!std::filesystem::is_directory(prefix, ec)) {
      completions->push_back(prefix);
      return;
    }

    path = std::filesystem::path(prefix) / "";
  } else {
    auto path_parts = std::filesystem::path(prefix);
    filename = path_parts.filename();

    if (filename.empty()) {
      return;
    }

    path = path_parts.parent_path();

    if (path.empty()) {
      path = std::filesystem::current_path(ec);

      if (ec) {
        return;
      }
    } else if (!std::filesystem::is_directory(path, ec)) {
      return;
    }
  }

  for (const auto& item : std::filesystem::directory_iterator(path, ec)) {
    auto found = std::string(item.path().filename());

    if (!StringBeginsWith(found, filename)) {
      continue;
    }

    auto completion = prefix + found.substr(filename.size());

    if (item.is_directory(ec)) {
      completion += "/";
    }

    completions->push_back(completion);
  }
}

// disconnect --------------------------------------------------------------------------------------

const char kDisconnectShortHelp[] = R"(disconnect: Disconnect from the remote system.)";
const char kDisconnectHelp[] =
    R"(disconnect

  Disconnects from the remote system, or cancels an in-progress connection if
  there is one.

  There are no arguments.
)";

Err DoDisconnect(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"disconnect\" takes no arguments.");

  context->session()->Disconnect([callback = std::move(callback)](const Err& err) mutable {
    if (err.has_error())
      Console::get()->Output(err);
    else
      Console::get()->Output("Disconnected successfully.");

    // We call the given callback
    if (callback)
      callback(err);
  });

  return Err();
}

// cls ---------------------------------------------------------------------------------------------

const char kClsShortHelp[] = "cls: clear screen.";
const char kClsHelp[] =
    R"(cls

  Clears the contents of the console. Similar to "clear" on a shell.

  There are no arguments.
)";

Err DoCls(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"cls\" takes no arguments.");

  Console::get()->Clear();

  if (callback)
    callback(Err());
  return Err();
}

// status ------------------------------------------------------------------------------------------

const char kStatusShortHelp[] = "status: Show debugger status.";
const char kStatusHelp[] = R"(status: Show debugger status.

  Shows information on the current connection, process, thread, etc. along
  with suggestions on what to do.
)";

Err DoStatus(ConsoleContext* context, const Command& cmd, CommandCallback cb = nullptr) {
  OutputBuffer out;
  out.Append(GetConnectionStatus(context->session()));
  out.Append("\n");

  if (!context->session()->IsConnected()) {
    Console::get()->Output(std::move(out));
    return Err();
  }

  out.Append(GetJobStatus(context));
  out.Append("\n");
  out.Append(GetProcessStatus(context));
  out.Append("\n");

  // Attempt to get the agent's state.
  context->session()->remote_api()->Status(
      {}, [out = std::move(out), session = context->session()->GetWeakPtr(), cb = std::move(cb)](
              const Err& err, debug_ipc::StatusReply reply) mutable {
        // Call the callback if applicable.
        Err return_err = err;
        auto defer = fit::defer([&return_err, cb = std::move(cb)]() mutable {
          if (cb)
            cb(return_err);
        });

        if (!session) {
          return_err = Err("No session found.");
          return;
        }

        if (err.has_error()) {
          return_err = err;
          return;
        }

        // Append the Limbo state.
        out.Append(GetLimboStatus(reply.limbo));

        Console::get()->Output(std::move(out));
      });

  return Err();
}

}  // namespace

void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kHelp] =
      VerbRecord(&DoHelp, {"help", "h"}, kHelpShortHelp, kHelpHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuit] =
      VerbRecord(&DoQuit, {"quit", "q", "exit"}, kQuitShortHelp, kQuitHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kConnect] =
      VerbRecord(&DoConnect, {"connect"}, kConnectShortHelp, kConnectHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kDisconnect] = VerbRecord(&DoDisconnect, {"disconnect"}, kDisconnectShortHelp,
                                           kDisconnectHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuitAgent] = VerbRecord(&DoQuitAgent, {"quit-agent"}, kQuitAgentShortHelp,
                                          kQuitAgentHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kOpenDump] =
      VerbRecord(&DoOpenDump, &DoCompleteOpenDump, {"opendump"}, kOpenDumpShortHelp, kOpenDumpHelp,
                 CommandGroup::kGeneral, SourceAffinity::kNone);
  (*verbs)[Verb::kStatus] = VerbRecord(&DoStatus, {"status", "stat", "wtf"}, kStatusShortHelp,
                                       kStatusHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kCls] =
      VerbRecord(&DoCls, {"cls"}, kClsShortHelp, kClsHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
