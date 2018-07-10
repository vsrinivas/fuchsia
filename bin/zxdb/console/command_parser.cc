// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_parser.h"

#include <stdio.h>
#include <map>
#include <set>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/nouns.h"
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

bool IsTokenSeparator(char c) { return c == ' '; }

// Finds the record for the switch associated with long switch string (which
// includes the two leading dashes), or null if there is no match.
//
// The token can contain and equals sign. In this case, only the text
// preceeding the equals sign counts as the switch, and the index of the equals
// sign is placed into *equals_index. Otherwise *equals_index will be set to
// std::string::npos. This is to handle the fact that long switches can be
// expressed as either "--foo=bar" and "--foo bar".
const SwitchRecord* FindLongSwitch(const std::string& str,
                                   const std::vector<SwitchRecord>& switches,
                                   size_t* equals_index) {
  // Should have two leading dashes.
  FXL_DCHECK(str.size() >= 2 && str.substr(0, 2) == "--");

  // Extract the switch value (varing depend on presence of '='), not counting
  // the two leading dashes.
  *equals_index = str.find('=');
  std::string switch_str;
  if (*equals_index == std::string::npos) {
    switch_str = str.substr(2);
  } else {
    switch_str = str.substr(2, *equals_index - 2);
  }

  for (const auto& sr : switches) {
    if (sr.name == switch_str)
      return &sr;
  }
  return nullptr;
}

const SwitchRecord* FindSwitch(char ch,
                               const std::vector<SwitchRecord>& switches) {
  for (const auto& sr : switches) {
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
Err ConsumeNoun(const std::vector<std::string>& tokens, size_t* token_index,
                Command* output, bool* consumed) {
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
      return Err("Invalid index \"" + tokens[*token_index] + "\" for \"" +
                 NounToString(noun) + "\".");
    }
    (*token_index)++;
  }

  *consumed = true;
  output->SetNoun(noun, noun_index);
  return Err();
}

// Continue to consume nouns from the token stream until either no more tokens
// have been found or we reached the end of tokens.
//
// If successful, it will add the nouns to the command and will update the
// token_index to the next token to be evaluated.
Err ConsumeNouns(const std::vector<std::string>& tokens, size_t* token_index,
                 Command* output) {
  bool found_noun = false;  // Whether the next token(s) were nouns
  do {
    Err err = ConsumeNoun(tokens, token_index, output, &found_noun);
    if (err.has_error())
      return err;
  } while (found_noun && (*token_index < tokens.size()));

  return Err();
}

// Consumes the next token expecting to find a verb. If valid it will register
// the verb into the command and will advance the token_index variable.
//
// It's possible there's no verb in which case this will put null into
// *verb_record and return success.
//
// Will update a given pointer to the respective VerbRecord. We return by
// pointer because VerbRecords are unique and non-copyable/movable.
Err ConsumeVerb(const std::vector<std::string>& tokens, size_t* token_index_ptr,
                Command* output, const VerbRecord** verb_record) {
  // Reference makes the code easier to understand
  size_t& token_index = *token_index_ptr;
  const std::string& token = tokens[token_index];

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
  *verb_record = &found_verb->second;

  return Err();
}

// Consumes tokens and interprets them as switches. Each verb has a particular
// set of switches associated to it. The appeareance of another switch means
// the command is erroneous.
//
// If successful, it will set the switches to the command and will update the
// token_index to the next token to be evaluated.
Err ConsumeSwitches(const std::vector<std::string>& tokens,
                    size_t* token_index_ptr, Command* output,
                    const std::vector<SwitchRecord>& switches) {
  // Reference makes the code easier to understand
  size_t& token_index = *token_index_ptr;

  // Look for switches.
  while (token_index < tokens.size()) {
    const std::string& token = tokens[token_index];

    // "--" marks the end of switches.
    if (token == "--") {
      token_index++;
      break;
    }

    // Not a switch, everything else is an arg.
    if (token[0] != '-')
      break;

    if (token.size() == 1)
      return Err("Invalid switch \"-\".");

    const SwitchRecord* sw_record = nullptr;
    std::string value;
    bool next_token_is_value = false;

    if (token[1] == '-') {
      // Two-hyphen (--) switch.
      size_t equals_index = std::string::npos;
      sw_record = FindLongSwitch(token, switches, &equals_index);
      if (!sw_record)
        return Err("Unknown switch \"" + token + "\".");

      if (equals_index == std::string::npos) {
        // "--foo bar" format.
        next_token_is_value = sw_record->has_value;
      } else {
        // "--foo=bar" format.
        if (sw_record->has_value) {
          // Extract the token following the equals sign.
          value = token.substr(equals_index + 1);
        } else {
          return Err("The switch " + token.substr(0, equals_index) +
                     " does not take a value.");
        }
      }
    } else {
      // Single-dash token means one character.
      char switch_char = token[1];
      sw_record = FindSwitch(switch_char, switches);
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

  // Keep track of the next token to evaluate
  size_t token_index = 0;

  // We look for all the possible nouns within the tokens
  Err noun_err = ConsumeNouns(tokens, &token_index, output);
  if (noun_err.has_error())
    return noun_err;

  // If no more tokens, then no verb was specified (for example "process 2").
  if (token_index == tokens.size())
    return Err();

  // Check for verb and get a reference to its record if there is one.
  const VerbRecord* verb_record = nullptr;
  if (tokens[token_index].size() >= 1 && tokens[token_index][0] != '-') {
    // Not a switch, read verb.
    Err verb_err = ConsumeVerb(tokens, &token_index, output, &verb_record);
    if (verb_err.has_error())
      return verb_err;
  }

  // Switches.
  const std::vector<SwitchRecord>& switches =
      verb_record ? verb_record->switches : GetNounSwitches();
  Err switch_err = ConsumeSwitches(tokens, &token_index, output, switches);
  if (switch_err.has_error())
    return switch_err;

  // Every token left is an argument to the command
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
      prefix += tokens[i];
      prefix.push_back(' ');
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
