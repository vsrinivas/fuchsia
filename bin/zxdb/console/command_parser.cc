// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_parser.h"

#include <map>
#include <set>
#include <stdio.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

namespace {

// Returns a sorted list of all possible noun and verb strings that can be
// input.
const std::set<std::string>& GetAllNounVerbStrings() {
  static std::set<std::string> strings;
  if (strings.empty()) {
    for (const auto& noun_pair : GetNouns()) {
      for (const auto& alias : noun_pair.second.aliases)
        strings.insert(alias);
    }
    for (const auto& verb_pair : GetVerbs()) {
      for (const auto& alias : verb_pair.second.aliases)
        strings.insert(alias);
    }
  }
  return strings;
}

// Returns only the canonical version of each noun and verb. Used for
// completions when there is no input and we don't want to cycle through both
// "s" and "step".
const std::set<std::string>& GetCanonicalNounVerbStrings() {
  static std::set<std::string> strings;
  if (strings.empty()) {
    for (const auto& noun_pair : GetNouns())
      strings.insert(noun_pair.second.aliases[0]);
    for (const auto& verb_pair : GetVerbs())
      strings.insert(verb_pair.second.aliases[0]);
  }
  return strings;

}

bool IsTokenSeparator(char c) {
  return c == ' ';
}

// Finds the record for the switch associated with string (which should not
// include leading dashes),
// or null if there is no match.
const SwitchRecord* FindSwitch(const std::string& str,
                               const VerbRecord& record) {
  for (const auto& sr : record.switches) {
    if (sr.name == str)
      return &sr;
  }
  return nullptr;
}

const SwitchRecord* FindSwitch(char ch, const VerbRecord& record) {
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

// Returns true if the string is all numeric digits that mean it's an index
// token.
bool IsIndexToken(const std::string& token) {
  for (char c : token) {
    if (c < '0' || c > '9')
      return false;
  }
  return true;
}

// Consumes the next noun (and optional following integer) in the input at
// *token_index. If valid, fills the information into the given command and
// advances *token_index to the next unused token.
//
// *consumed will be set if any nouns were consumed (to disambiguate the
// "error parsing" case and the "the next thing wasn't a noun" case).
Err ConsumeNoun(const std::vector<std::string>& tokens,
                size_t* token_index,
                Command* output,
                bool* consumed) {
  *consumed = false;

  const auto& nouns = GetStringNounMap();
  auto found = nouns.find(tokens[*token_index]);
  if (found == nouns.end())
    return Err();  // Not a noun, but that's not an error.

  Noun noun = found->second;
  if (output->HasNoun(noun))
    return Err("Noun \"" + NounToString(noun) + "\" specified twice.");

  // Advance to the next token.
  (*token_index)++;

  // Consume optional following index if it's all integers. For example, it
  // could be "process 2 run" (with index) or "process run" (without).
  size_t noun_index = Command::kNoIndex;
  if ((*token_index) < tokens.size() && IsIndexToken(tokens[*token_index])) {
    if (sscanf(tokens[*token_index].c_str(), "%zu", &noun_index) != 1) {
      return Err("Invalid index \"" + tokens[*token_index] +
                 "\" for \"" + NounToString(noun) + "\".");
    }
    (*token_index)++;
  }

  *consumed = true;
  output->SetNoun(noun, noun_index);
  return Err();
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

  size_t token_index = 0;
  while (token_index < tokens.size()) {
    bool had_noun = false;
    Err err = ConsumeNoun(tokens, &token_index, output, &had_noun);
    if (err.has_error())
      return err;
    if (!had_noun)
      break;
  }

  if (token_index == tokens.size())
    return Err();  // No verb specified (for example "process 2").

  // Consume the verb.
  const auto& verb_strings = GetStringVerbMap();
  auto found_verb_str = verb_strings.find(tokens[token_index]);
  if (found_verb_str == verb_strings.end()) {
    return Err("The string \"" + tokens[token_index] +
               "\" is not a valid verb.");
  }
  output->set_verb(found_verb_str->second);
  token_index++;

  // Find the verb record.
  const auto& verbs = GetVerbs();
  auto found_verb = verbs.find(output->verb());
  FXL_DCHECK(found_verb != verbs.end());  // Valid verb should always be found.
  const VerbRecord& verb_record = found_verb->second;

  // Look for switches.
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
      sw_record = FindSwitch(token.substr(2), verb_record);
      if (!sw_record)
        return Err(std::string("Unknown switch \"") + token + "\".");
      next_token_is_value = sw_record->has_value;
    } else {
      // Single-dash token means one character.
      char switch_char = token[1];
      sw_record = FindSwitch(switch_char, verb_record);
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
    output->SetSwitch(sw_record->id, std::move(value));

    token_index++;
  }

  std::vector<std::string> args(tokens.begin() + token_index, tokens.end());
  output->set_args(std::move(args));
  return Err();
}

// It would be nice to do more context-aware completions. For now, just
// complete based on all known nouns and verbs.
std::vector<std::string> GetCommandCompletions(const std::string& input) {
  std::vector<std::string> result;

  std::vector<std::string> tokens;
  Err err = TokenizeCommand(input, &tokens);
  if (err.has_error())
    return result;

  // The no input or following a space, cycle through all possibilities.
  if (input.empty() || tokens.empty() || input.back() == ' ') {
    for (const auto& str : GetCanonicalNounVerbStrings())
      result.push_back(input + str);
    return result;
  }

  // Compute the string of stuff that stays constant for each completion.
  std::string prefix;
  if (!tokens.empty()) {
    // All tokens but the last one.
    for (size_t i = 0; i < tokens.size() - 1; i++) {
      if (i > 0)
        prefix.push_back(' ');
      prefix += tokens[i];
    }
  }

  // Cycle through matching prefixes.
  const std::string token = tokens.back();
  const std::set<std::string>& possibilities = GetAllNounVerbStrings();
  auto found = possibilities.lower_bound(token);
  while (found != possibilities.end() && StartsWith(*found, token)) {
    result.push_back(prefix + *found);
    ++found;
  }

  return result;
}

}  // namespace zxdb
