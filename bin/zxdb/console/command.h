// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace zxdb {

class Err;
class Process;
class Session;
class Thread;

// Noun ------------------------------------------------------------------------

enum class Noun {
  kNone = 0,
  kFrame,
  kThread,
  kProcess,

  // Adding a new one? Add to GetNouns().
  kLast  // Not a real noun, keep last.
};

std::string NounToString(Noun n);

// Verb ------------------------------------------------------------------------

// Note: things to add: kAttach, kBacktrace, kContinue, kDelete, kDown, kList,
// kListProcesses, kRead, kSet, kStepIn, kStepInst, kStepOut, kStepOver, kUp,
// kWrite,
enum class Verb {
  kNone = 0,

  kHelp,
  kListProcesses,
  kMemRead,
  kQuit,
  kRun,

  // Adding a new one? Add in one of the functions GetVerbs() calls.
  kLast  // Not a real verb, keep last.
};

std::string VerbToString(Verb v);

class Command {
 public:
  // This valid indicates that there was a noun specified but no index.
  // For example the command "process step" specifies the process noun but
  // not an index.
  static constexpr size_t kNoIndex = (size_t)(-1);

  Command();
  ~Command();

  // Returns true if the noun was specified by the user.
  bool HasNoun(Noun noun) const;

  // Returns the index specified for the given noun. If the noun was not
  // specified or the index was not specified, returns kNoIndex (use HasNoun to
  // disambiguate).
  size_t GetNounIndex(Noun noun) const;

  // Sets that the given noun was present. index may be kNoIndex.
  void SetNoun(Noun noun, size_t index);

  const std::map<Noun, size_t>& nouns() const { return nouns_; }

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
  Process* process() const { return process_; }
  void set_process(Process* p) { process_ = p; }
  Thread* thread() const { return thread_; }
  void set_thread(Thread* t) { thread_ = t; }

 private:
  // The nouns specified for this command. If not present here, the noun
  // was not written on the command line. If present but ther was no index
  // given for it, the mapped value will be kNoIndex. Otherwise the mapped
  // value will be the index specified.
  std::map<Noun, size_t> nouns_;

  // The effective context for the command. The explicitly specified process/
  // thread/etc. will be reflected here, and anything that wasn't explicit
  // will inherit the default.
  Process* process_ = nullptr;  // Guaranteed non-null for valid commands.
  Thread* thread_ = nullptr;  // Will be null if not running.
  // Frame* frame_ = nullptr;  // TODO(brettw) do frame support.

  Verb verb_ = Verb::kNone;

  std::map<int, std::string> switches_;
  std::vector<std::string> args_;
};

// Switches --------------------------------------------------------------------

struct SwitchRecord {
  SwitchRecord();
  SwitchRecord(const SwitchRecord&);
  SwitchRecord(int i, bool has_value, const char* n, char c = 0);
  ~SwitchRecord();

  int id = 0;

  // Indicates if this switch has a value. False means it's a bool.
  bool has_value = false;

  // Not including hyphens, e.g. "size" for the switch "--size".
  const char* name = nullptr;

  // 1-character shorthand switch. 0 means no short variant.
  char ch = 0;
};

// Command dispatch ------------------------------------------------------------

// Type for the callback that runs a command.
using CommandExecutor = Err (*)(Session*, const Command& cmd);

struct NounRecord {
  NounRecord();
  NounRecord(std::initializer_list<std::string> aliases,
             const char* short_help,
             const char* help);
  ~NounRecord();

  // These are the user-typed strings that will name this noun. The [0]th one
  // is the canonical name.
  std::vector<std::string> aliases;

  const char* short_help = nullptr;  // One-line help.
  const char* help = nullptr;
};

struct VerbRecord {
  VerbRecord();

  // The help will be referenced by pointer. It is expected to be a static
  // string.
  VerbRecord(CommandExecutor exec,
             std::initializer_list<std::string> aliases,
             const char* short_help,
             const char* help);
  ~VerbRecord();

  CommandExecutor exec = nullptr;

  // These are the user-typed strings that will name this verb. The [0]th one
  // is the canonical name.
  std::vector<std::string> aliases;

  const char* short_help = nullptr;  // One-line help.
  const char* help = nullptr;
  std::vector<SwitchRecord> switches;  // Switches supported by this verb.
};

// Returns all known nouns. The contents of this map will never change once
// it is called.
const std::map<Noun, NounRecord>& GetNouns();

// Returns all known verbs. The contents of this map will never change once
// it is called.
const std::map<Verb, VerbRecord>& GetVerbs();

// Returns the mappping from possible inputs to the noun/verb. This is an
// inverted version of the map returned by GetNouns()/GetVerbs();
const std::map<std::string, Noun>& GetStringNounMap();
const std::map<std::string, Verb>& GetStringVerbMap();

// Runs the given command.
Err DispatchCommand(Session* session, const Command& cmd);

}  // namespace zxdb
