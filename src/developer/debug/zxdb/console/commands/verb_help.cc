// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_help.h"

#include <map>
#include <string>

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kExpressionsName[] = "expressions";
const char kExpressionsHelp[] = R"(Expressions

  Expressions appear in many commands. Some commands expect just an expression,
  most notably "print":

    [zxdb] print &object->array_data[i + 4]
    (*)71cc72b5310

  Other commands such as "break" or "disassemble" accept a location. This is
  typically a function name or a line number, but can also be an expression that
  evaluates to a memory address. To use expressions for these commands, prefix
  it with a "*":

    [zxdb] break *foo->some_address
    Created Breakpoint 1 @ 0x30ad01a2b80

  Most C++ and Rust operators are implemented in a language-compatible way.
  Function calls are not currently supported (with exceptions, see "Pretty
  printers" below). User-overloaded operators are ignored.

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

Common errors

  <Optimized out>
      Indicates that the program symbols declare a variable with the given name,
      but that it has no value or location. This means the compiler has entirely
      optimized out the variable and the debugger can not show it. If you need
      to see it, use a less-optimized build setting.

  <Unavailable>
      Indicates that the variable is not valid at the current address, but that
      its value is known at other addresses. In optimized code, the compiler
      will often re-use registers, clobbering previous values which become
      unavailable.

      You can see the valid ranges for a variable with the "sym-info" command:

        [zxdb] sym-info my_variable

      Under "DWARF location" it will give a list of address ranges where the
      value of the variable is known (inclusive at the beginning of the range,
      non-inclusive at the end). Run to one of these addresses to see the value
      of the variable (use "di" to see the current address).

      You can ignore the "DWARF expression bytes" which are the internal
      instructions for finding the variable.
)";

constexpr char kJitdName[] = "jitd";
constexpr char kJitdHelp[] = R"(Just In Time Debugging

  Just In Time Debugging is a way for the system to suspend processes that have
  crashed without any exception handlers. The system will keep those processes
  in a place called "Process Limbo". Later, zxdb is able to connect to Process
  Limbo and attach to process waiting to be debugged.

Enabling process limbo in the system

  To enable catching exceptions in newly crashed processes, type in a Fuchsia
  shell:

    run limbo.cmx enable

  For full documentation on enabling and configuring Limbo, including enabling
  on system startup, see the full documentation at:

  https://fuchsia.dev/fuchsia-src/development/debugger/just_in_time_debugging.md

Listing Processes

  When zxdb starts up, any process waiting to be debugged within Process Limbo
  will be listed like this:

    👉 To get started, try "status" or "help".
    Processes waiting on exception:
    2780309: process-that-crashed
    2783544: some-other-process-that-crashed
    Type "attach <pid>" to reconnect.
    [zxdb]

  You call also run the "status" command and get the same information:

    [zxdb] status
    ...
    Processes waiting on exception
    2 process(es) waiting on exception.
      Run "attach <KOID>" to load them into zxdb or "detach <KOID>" to
      terminate them. See "help jitd" for more information on Just-In-Time
      Debugging.

     2780309 process-that-crashed
     2783544 some-other-process-that-crashed

Attaching/Removing Processes

  From the point of view of zxdb, the processes within the limbo behave very
  similar to what a normal running process does. In order to start debugging
  one, simply do "attach <KOID>" and zxdb will retrieve the process from limbo
  and start debugging it. Once attached, you can manipulate the process as
  normal, and even detach or kill it.

  Note that if you detach from a crashing process, the exception will be
  re-triggered and it will caught by the Process Limbo. Killing it will
  terminate the process as usual.

  The only difference comes when attempting to release a process from the
  Process Limbo, without attaching from it. In that case, you need to instruct
  the debugger to "detach" from it by issuing a "detach <KOID>" command.
)";

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

// Sorted list of strings for other help topics.
const char* kOtherTopics[] = {
    "expressions: Information on expressions used in \"print\", etc.",
    "jitd: Use \"just-in-time debugging\" to attach after a process crashes.",
};

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
  help.Append(" (see \"help <topic>\")\n\n");
  for (const auto& line : kOtherTopics)
    help.Append(FormatIndexLine(line));

  help.Append(Syntax::kHeading, "\nCommand syntax\n");
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

Err RunVerbHelp(ConsoleContext* context, const Command& cmd) {
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
      } else if (on_what == kJitdName) {
        help = kJitdHelp;
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

}  // namespace

VerbRecord GetHelpVerbRecord() {
  return VerbRecord(&RunVerbHelp, {"help", "h"}, kHelpShortHelp, kHelpHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
