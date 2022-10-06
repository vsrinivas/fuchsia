// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_H_

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

class Breakpoint;
class ConsoleContext;
class Filter;
class Frame;
class Target;
class Thread;
class SymbolServer;

// Command ---------------------------------------------------------------------

class Command {
 public:
  // This valid indicates that there was a noun specified but no index.
  // For example the command "process step" specifies the process noun but
  // not an index.
  static constexpr int kNoIndex = -1;

  Command();
  ~Command();

  // Returns true if the noun was specified by the user.
  bool HasNoun(Noun noun) const;

  // Returns the index specified for the given noun. If the noun was not
  // specified or the index was not specified, returns kNoIndex (use HasNoun to
  // disambiguate).
  int GetNounIndex(Noun noun) const;

  // Sets that the given noun was present. index may be kNoIndex.
  void SetNoun(Noun noun, int index);

  const std::map<Noun, int>& nouns() const { return nouns_; }

  // Checks the specified nouns against the parameter listing the allowed ones.
  // If any nouns are specified that are not in the list, generates an error
  // and returns it. Otherwise it will return an empty error.
  Err ValidateNouns(std::initializer_list<Noun> allowed_nouns) const;

  Verb verb() const { return verb_; }
  void set_verb(Verb v) { verb_ = v; }

  // Returns whether a given switch was specified.
  bool HasSwitch(int id) const;

  // Returns the value corresponding to the given switch, or the empty string
  // if not specified.
  std::string GetSwitchValue(int id) const;

  void SetSwitch(int id, std::string str);

  const std::map<int, std::string>& switches() const { return switches_; }

  const std::vector<std::string>& args() const { return args_; }
  void set_args(std::vector<std::string> a) { args_ = std::move(a); }

  // The computed environment for the command. This is filled in with the
  // objects corresponding to the indices given on the command line, and
  // default to the current one for the current command line.
  //
  // If HasNoun() returns true, the corresponding getter here is guaranteed
  // non-null.
  Frame* frame() const { return frame_; }
  void set_frame(Frame* f) { frame_ = f; }
  Target* target() const { return target_; }
  void set_target(Target* t) { target_ = t; }
  Thread* thread() const { return thread_; }
  void set_thread(Thread* t) { thread_ = t; }
  Breakpoint* breakpoint() const { return breakpoint_; }
  void set_breakpoint(Breakpoint* b) { breakpoint_ = b; }
  Filter* filter() const { return filter_; }
  void set_filter(Filter* b) { filter_ = b; }
  SymbolServer* sym_server() const { return symbol_server_; }
  void set_sym_server(SymbolServer* s) { symbol_server_ = s; }

 private:
  // The nouns specified for this command. If not present here, the noun
  // was not written on the command line. If present but there was no index
  // given for it, the mapped value will be kNoIndex. Otherwise the mapped
  // value will be the index specified.
  std::map<Noun, int> nouns_;

  // The effective context for the command. The explicitly specified process/
  // thread/etc. will be reflected here, and anything that wasn't explicit
  // will inherit the default.
  Target* target_ = nullptr;               // Guaranteed non-null for valid commands.
  Thread* thread_ = nullptr;               // Will be null if not running.
  Frame* frame_ = nullptr;                 // Will be null if no valid thread stopped.
  Breakpoint* breakpoint_ = nullptr;       // May be null.
  Filter* filter_ = nullptr;               // May be null.
  SymbolServer* symbol_server_ = nullptr;  // May be null.

  Verb verb_ = Verb::kNone;

  std::map<int, std::string> switches_;
  std::vector<std::string> args_;
};

// Command dispatch ------------------------------------------------------------

// Type for a callback that a CommandExecutor will receive
using CommandCallback = fit::callback<void(Err)>;

// Runs the given command.
void DispatchCommand(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_H_
