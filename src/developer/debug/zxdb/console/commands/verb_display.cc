// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_display.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kDisplayShortHelp[] = "display: Print an expression on every stop.";
const char kDisplayHelp[] =
    R"(display <expression>

  Adds the given expression to the global list that will be evaluated and
  printed for every stop.

The "display" setting

  This command is syntactic sugar for the settings system where the setting
  named "display" keeps this information. Use the settings system to view or
  remove expressions (there is no "undisplay" command).

  The settings system is hierarchical (see "help get" for more) so there are
  global, process, and thread-specific display lists. The most specific nonempty
  list will be used when a thread stops.

  For more complex ways to modify the display list, use the settings system (see
  "help set"). Note that expressions with spaces will need to be quoted when
  used with the settings system, but this is not necessary when using the
  "display" verb since it can only add one expression at a time.

Examples

  display foo->bar
  set display += "foo->bar"
  global set display += "foo->bar"
      These commands are equivalent to add the expression to the global
      "display" list.

  get display
      Prints the current thread's "display" list. This will fall back on the
      process' list, and then on the global list if unset.

  set display=
      Clears all variables from the global display list.

  set display -= "foo->bar"
      Removes the given expression from the display list, keeping others the
      same.

  thread set display += i
  thread get display
  thread set display -= i
  thread set display =
      Adds, prints, removes, and clears the thread-specific display list. These
      are the same as the above examples but with "thread" added to the
      beginning. If there is a thread-specific display list, it will take effect
      whenever that thread stops instead of process or global ones.

  process set display = i j "foo->bar"
      Overwrites all contents of the process-specific display list to print the
      given three expressions. The process list will take effect when the
      process stops and there is no thread-specific one.
)";

const char kCommandHelp[] =
    " • View current list: get display\n"
    " • Remove one:        set display -= your_var\n"
    " • Clear list:        set display =\n";

Err RunVerbDisplay(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() != 1) {
    // We could print the current list fo stuff to display here like GDB, but would prefer that
    // people learn to interact with the settings system since they'll need that to remove values
    // anyway.
    return Err(
        "The \"display\" verb is syntactic sugar for the settings system's \"display\"\n"
        "setting. It's a shortcut to add expressions to display, but otherwise use the\n"
        "settings commands:\n" +
        std::string(kCommandHelp));
  }

  // The thing to watch. Note that we can't actually validate this here because the expression might
  // only be valid in a different context.
  const std::string& new_expression = cmd.args()[0];

  SettingStore* store = &context->session()->system().settings();

  // Be nice and avoid duplicating an expression.
  std::vector<std::string> list = store->GetList(ClientSettings::Thread::kDisplay);
  for (const auto& existing : list) {
    if (new_expression == existing)
      return Err("Already watching expression \"" + new_expression + "\".");
  }

  list.push_back(new_expression);
  store->SetList(ClientSettings::Thread::kDisplay, list);

  OutputBuffer out("Added to display for every stop: ");
  out.Append(Syntax::kHeading, new_expression);
  out.Append("\n");
  out.Append(kCommandHelp);

  Console::get()->Output(out);
  return Err();
}

}  // namespace

VerbRecord GetDisplayVerbRecord() {
  VerbRecord record(&RunVerbDisplay, {"display"}, kDisplayShortHelp, kDisplayHelp,
                    CommandGroup::kQuery);
  record.param_type = VerbRecord::kOneParam;  // Allows arbitrary unquoted input.
  return record;
}

}  // namespace zxdb
