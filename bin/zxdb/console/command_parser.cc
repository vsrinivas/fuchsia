// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_parser.h"

#include <map>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"

namespace zxdb {

namespace {

// Stores an expansion from a short command to a noun ("none" verb) or a
// noun/verb pair. In the
// noun-only case, the command expects an explicit verb to follow.
struct Shortcut {
  Shortcut() = default;
  Shortcut(Noun n) : noun(n), verb(Verb::kNone) {}
  Shortcut(Noun n, Verb v) : noun(n), verb(v) {}

  Noun noun = Noun::kNone;
  Verb verb = Verb::kNone;
};

using ShortcutMap = std::map<std::string, Shortcut>;

const ShortcutMap& GetShortcutMap() {
  static ShortcutMap map;
  if (!map.empty())
    return map;

  // Nouns.
  map["breakpoint"] = Shortcut(Noun::kBreakpoint);
  map["frame"] = Shortcut(Noun::kFrame);
  map["memory"] = Shortcut(Noun::kMemory);
  map["process"] = Shortcut(Noun::kProcess);
  map["system"] = Shortcut(Noun::kSystem);
  map["thread"] = Shortcut(Noun::kThread);
  map["zxdb"] = Shortcut(Noun::kZxdb);

  // Noun-only shortcuts.
  map["fr"] = Shortcut(Noun::kFrame);
  map["br"] = Shortcut(Noun::kBreakpoint);
  map["mem"] = Shortcut(Noun::kMemory);
  map["pro"] = Shortcut(Noun::kProcess);
  map["th"] = Shortcut(Noun::kThread);

  // Noun-verb shortcuts.
  map["attach"] = Shortcut(Noun::kProcess, Verb::kAttach);
  map["b"] = Shortcut(Noun::kBreakpoint, Verb::kSet);
  map["break"] = Shortcut(Noun::kBreakpoint, Verb::kSet);
  map["bt"] = Shortcut(Noun::kBreakpoint, Verb::kBacktrace);
  map["c"] = Shortcut(Noun::kThread, Verb::kContinue);
  map["continue"] = Shortcut(Noun::kThread, Verb::kContinue);
  map["delete"] = Shortcut(Noun::kBreakpoint, Verb::kDelete);
  map["down"] = Shortcut(Noun::kFrame, Verb::kDown);
  map["f"] = Shortcut(Noun::kFrame, Verb::kSelect);
  map["finish"] = Shortcut(Noun::kThread, Verb::kStepOut);
  map["g"] = Shortcut(Noun::kThread, Verb::kContinue);
  map["help"] = Shortcut(Noun::kZxdb, Verb::kHelp);
  map["n"] = Shortcut(Noun::kThread, Verb::kStepOver);
  map["next"] = Shortcut(Noun::kThread, Verb::kStepOver);
  map["ps"] = Shortcut(Noun::kSystem, Verb::kListProcesses);
  map["q"] = Shortcut(Noun::kZxdb, Verb::kQuit);
  map["quit"] = Shortcut(Noun::kZxdb, Verb::kQuit);
  map["r"] = Shortcut(Noun::kProcess, Verb::kRun);
  map["run"] = Shortcut(Noun::kProcess, Verb::kRun);
  map["s"] = Shortcut(Noun::kThread, Verb::kStepIn);
  map["si"] = Shortcut(Noun::kThread, Verb::kStepInst);
  map["step"] = Shortcut(Noun::kThread, Verb::kStepIn);
  map["stepi"] = Shortcut(Noun::kThread, Verb::kStepInst);
  map["up"] = Shortcut(Noun::kFrame, Verb::kUp);

  static_assert(static_cast<int>(Noun::kLast) == 8,
                "Need to update GetShortcutMap for noun addition.");

  return map;
}

using VerbMap = std::map<std::string, Verb>;

const VerbMap& GetVerbMap() {
  static VerbMap map;
  if (!map.empty())
    return map;

  // Full verbs.
  map["attach"] = Verb::kAttach;
  map["backtrace"] = Verb::kBacktrace;
  map["continue"] = Verb::kContinue;
  map["delete"] = Verb::kDelete;
  map["down"] = Verb::kDown;
  map["help"] = Verb::kHelp;
  map["list"] = Verb::kList;
  map["list-processes"] = Verb::kListProcesses;
  map["quit"] = Verb::kQuit;
  map["read"] = Verb::kRead;
  map["run"] = Verb::kRun;
  map["select"] = Verb::kSelect;
  map["set"] = Verb::kSet;
  map["step-in"] = Verb::kStepIn;
  map["step-inst"] = Verb::kStepInst;
  map["step-out"] = Verb::kStepOut;
  map["step-over"] = Verb::kStepOver;
  map["up"] = Verb::kUp;
  map["write"] = Verb::kWrite;

  // Verb shortcuts.
  map["at"] = Verb::kAttach;
  map["del"] = Verb::kDelete;
  map["l"] = Verb::kList;
  map["ls"] = Verb::kList;
  map["ps"] = Verb::kListProcesses;
  map["set"] = Verb::kSelect;

  static_assert(static_cast<int>(Verb::kLast) == 20,
                "Need to update GetVerbMap for noun addition.");

  return map;
}

bool IsTokenSeparator(char c) {
  return c == ' ';
}

// Finds the record for the switch associated with string (which should not
// include leading dashes),
// or null if there is no match.
const SwitchRecord* FindSwitch(const std::string& str,
                               const CommandRecord& record) {
  for (const auto& sr : record.switches) {
    if (sr.name == str)
      return &sr;
  }
  return nullptr;
}

const SwitchRecord* FindSwitch(char ch, const CommandRecord& record) {
  for (const auto& sr : record.switches) {
    if (sr.ch == ch)
      return &sr;
  }
  return nullptr;
}

bool StartsWith(const std::string& str, const std::string& starts_with) {
  if (str.size() < starts_with.size())
    return false;
  for (size_t i = 0; i < starts_with.size(); i++) {
    if (str[i] != starts_with[i])
      return false;
  }
  return true;
}

std::vector<std::string> GetNounCompletions(const std::string& token) {
  std::vector<std::string> result;
  const ShortcutMap& shortcut_map = GetShortcutMap();

  auto found_shortcut = shortcut_map.lower_bound(token);
  while (found_shortcut != shortcut_map.end() &&
         StartsWith(found_shortcut->first, token)) {
    result.push_back(found_shortcut->first);
    ++found_shortcut;
  }
  return result;
}

}  // namespace

Err TokenizeCommand(const std::string& input,
                    std::vector<std::string>* result) {
  result->clear();

  // TODO(brettw) this will probably need some kind of quoting and escaping
  // logic.
  size_t cur = 0;
  while (true) {
    // Skip separators
    while (cur < input.size() && IsTokenSeparator(input[cur])) {
      ++cur;
    }
    if (cur == input.size())
      break;
    size_t token_begin = cur;

    // Skip to end of token.
    while (cur < input.size() && !IsTokenSeparator(input[cur])) {
      ++cur;
    }
    if (cur == token_begin)
      break;  // Got to end of input.

    // Emit token.
    result->push_back(input.substr(token_begin, cur - token_begin));
  }

  // This returns an Err() to allow for adding escaping errors in the future.
  return Err();
}

Err ParseCommand(const std::string& input, Command* output) {
  *output = Command();

  std::vector<std::string> tokens;
  Err err = TokenizeCommand(input, &tokens);
  if (err.has_error() || tokens.empty())
    return err;

  return ParseCommand(tokens, output);
}

Err ParseCommand(const std::vector<std::string>& tokens, Command* output) {
  *output = Command();
  if (tokens.empty())
    return Err();

  // Loop up the root noun.
  const ShortcutMap& shortcut_map = GetShortcutMap();
  auto found_shortcut = shortcut_map.find(tokens[0]);
  if (found_shortcut == shortcut_map.end()) {
    return Err("Unknown command \"" + tokens[0] + "\". See \"help\".");
  }

  // Handle verbs.
  size_t first_arg;
  if (found_shortcut->second.verb == Verb::kNone && tokens.size() >= 2) {
    const VerbMap& verb_map = GetVerbMap();
    auto found_verb = verb_map.find(tokens[1]);
    if (found_verb == verb_map.end()) {
      return Err("Unknown verb. See \"help " +
                 std::string(NounToString(found_shortcut->second.noun)) +
                 "\".");
    }
    output->verb = found_verb->second;
    first_arg = 2;
  } else {
    output->verb = found_shortcut->second.verb;
    first_arg = 1;
  }
  output->noun = found_shortcut->second.noun;

  // Get the command record for this noun/verb. This also validates that the
  // verb is valid.
  const CommandRecord& record = GetRecordForCommand(*output);
  if (!record.exec) {
    std::string noun_str = NounToString(output->noun);
    std::string verb_str = VerbToString(output->verb);
    return Err("Invalid combination \"" + noun_str + " " + verb_str +
               "\". See \"help " + noun_str + "\".");
  }

  // Look for switches.
  size_t token_index = first_arg;
  while (token_index < tokens.size()) {
    const std::string& token = tokens[token_index];
    if (token == "--") {
      // "--" marks the end of switches.
      token_index++;
      break;
    }
    if (token[0] != '-')
      break;  // Not a switch, everything else is an arg.
    if (token.size() == 1)
      return Err("Invalid switch \"-\".");

    const SwitchRecord* sw_record = nullptr;
    std::string value;
    bool next_token_is_value = false;
    if (token[1] == '-') {
      // Two-hyphen switch.
      sw_record = FindSwitch(token.substr(2), record);
      if (!sw_record)
        return Err(std::string("Unknown switch \"") + token + "\".");
      next_token_is_value = sw_record->has_value;
    } else {
      // Single-dash token means one character.
      char switch_char = token[1];
      sw_record = FindSwitch(switch_char, record);
      if (!sw_record)
        return Err(std::string("Unknown switch \"-") + switch_char + "\".");

      if (token.size() > 2) {
        // Single character switch with stuff after it: it's the argument "-a4"
        if (!sw_record->has_value) {
          return Err(std::string("Extra characters after \"-") + switch_char +
                     "\".");
        }
        value = token.substr(2);
      } else {
        next_token_is_value = sw_record->has_value;
      }
    }

    // Expecting a value as the next token.
    if (next_token_is_value) {
      if (token_index == tokens.size() - 1) {
        // No more tokens to consume.
        return Err(std::string("Parameter needed for \"") + token + "\".");
      } else {
        token_index++;
        value = tokens[token_index];
      }
    }
    output->switches[sw_record->id] = value;

    token_index++;
  }

  output->args.insert(output->args.end(), tokens.begin() + token_index,
                      tokens.end());
  return Err();
}

std::vector<std::string> GetCommandCompletions(const std::string& input) {
  std::vector<std::string> result;

  std::vector<std::string> tokens;
  Err err = TokenizeCommand(input, &tokens);
  if (err.has_error() || tokens.empty())
    return result;
  bool ends_in_space = input.back() == ' ';

  // Only one token means completion based on the noun. If it ends in a space,
  // assume they're done typing the noun and want verb completions.
  if (tokens.size() == 1 && !ends_in_space)
    return GetNounCompletions(tokens[0]);

  // Look up this noun.
  const ShortcutMap& shortcut_map = GetShortcutMap();
  auto found_shortcut = shortcut_map.find(tokens[0]);
  if (found_shortcut == shortcut_map.end())
    return result;

  if (found_shortcut->second.verb == Verb::kNone) {
    // This noun needs a verb.
    const auto& verbs = GetVerbsForNoun(found_shortcut->second.noun);
    if (tokens.size() == 1 && ends_in_space) {
      // Complete based on all verbs for this noun.
      for (const auto& pair : verbs) {
        if (pair.first != Verb::kNone)
          result.push_back(tokens[0] + " " + VerbToString(pair.first));
      }
    } else if (tokens.size() == 2 && !ends_in_space) {
      // Complete based on verb prefixes.
      for (const auto& pair : verbs) {
        if (pair.first != Verb::kNone) {
          std::string verb_str = VerbToString(pair.first);
          if (StartsWith(verb_str, tokens[1]))
            result.push_back(tokens[0] + " " + verb_str);
        }
      }
      return result;
    }
  }

  return result;
}

}  // namespace zxdb
