// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_parser.h"

#include <stdio.h>

#include <map>
#include <set>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

bool IsTokenSeparator(char c) { return c == ' '; }

// Finds the record for the switch associated with long switch string (which
// includes the two leading dashes), or null if there is no match.
//
// The token can contain and equals sign. In this case, only the text
// preceding the equals sign counts as the switch, and the index of the equals
// sign is placed into *equals_index. Otherwise *equals_index will be set to
// std::string::npos. This is to handle the fact that long switches can be
// expressed as either "--foo=bar" and "--foo bar".
const SwitchRecord* FindLongSwitch(const std::string& str,
                                   const std::vector<SwitchRecord>& switches,
                                   size_t* equals_index) {
  // Should have two leading dashes.
  FXL_DCHECK(str.size() >= 2 && str.substr(0, 2) == "--");

  // Extract the switch value (varying depend on presence of '='), not counting
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

const SwitchRecord* FindSwitch(char ch, const std::vector<SwitchRecord>& switches) {
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

class Parser {
 public:
  Parser(const std::string& input, Command* command, const FillCommandContextCallback& fill_context)
      : input_(input), fill_context_(fill_context), command_(command) {}

  const Err& Parse() {
    if (!Tokenize(input_)) {
      return err_;
    }

    while (Advance())
      ;

    return err_;
  }

  const Err& Complete(std::vector<std::string>* results) {
    FXL_DCHECK(results->empty());

    if (!Tokenize(input_)) {
      return err_;
    }

    std::string to_complete;

    if (!tokens_.empty() && input_.back() != ' ') {
      to_complete = tokens_.back().str;
      tokens_.pop_back();
    }

    while (Advance())
      ;

    if (!at_end()) {
      return err_;
    }

    for (const auto& state : states_at_pos_) {
      (this->*(state->complete))(to_complete, results);
    }

    std::string prefix = input_.substr(0, input_.size() - to_complete.size());
    for (auto& result : *results) {
      result = prefix + result;
    }

    err_ = Err();
    return err_;
  }

  Err err() { return err_; }

  bool at_end() { return tokens_.size() == pos_; }

 private:
  struct State {
    bool (Parser::*advance)();

    // Completer callback. The first argument is a prefix we expect all
    // completions to share. The second is a vector to which we should append
    // full single tokens which would be valid completions.
    void (Parser::*complete)(const std::string&, std::vector<std::string>*);

    State(bool (Parser::*advance)(),
          void (Parser::*complete)(const std::string&, std::vector<std::string>*))
        : advance(advance), complete(complete) {}
  };

  const std::string& token_str() { return tokens_[pos_].str; }

  bool Advance() {
    const State* start = state_;
    size_t start_pos = pos_;
    bool result = (this->*(state_->advance))();

    if (start_pos != pos_) {
      states_at_pos_.clear();
    } else {
      states_at_pos_.push_back(start);
    }

    return result;
  }

  bool Consume(const State& state) {
    state_ = &state;
    return Consume();
  }

  bool GoTo(const State& state) {
    state_ = &state;
    return true;
  }

  bool Consume() {
    pos_++;
    return true;
  }

  bool Fail(const std::string& msg) {
    err_ = Err(msg);
    return false;
  }

  bool Accept() { return false; }

  bool DoNounState();
  bool DoNounIndexState();
  bool DoVerbState();
  bool DoSwitchesState();
  bool DoSwitchState();
  bool DoLongSwitchState();
  bool DoSwitchArgState();
  bool DoArgState();

  void NoComplete(const std::string&, std::vector<std::string>*) {}

  void DoCompleteNoun(const std::string& to_complete, std::vector<std::string>* result);
  void DoCompleteSwitches(const std::string& to_complete, std::vector<std::string>* result);
  void DoCompleteVerb(const std::string& to_complete, std::vector<std::string>* result);
  void DoCompleteArgs(const std::string& to_complete, std::vector<std::string>* result);

  static const State kNounState;
  static const State kNounIndexState;
  static const State kVerbState;
  static const State kSwitchesState;
  static const State kSwitchState;
  static const State kLongSwitchState;
  static const State kSwitchArgState;
  static const State kArgState;

  bool Tokenize(const std::string& input) {
    FXL_DCHECK(!err_.has_error() && pos_ == 0 && tokens_.size() == 0);
    err_ = TokenizeCommand(input, &tokens_);
    return !err_.has_error();
  }

  const std::string input_;

  // Used for completions. Optionally set to fill in the noun information for
  // a given command so the completion code can use them.
  const FillCommandContextCallback& fill_context_;

  Command* command_ = nullptr;
  const State* state_ = &kNounState;
  Err err_;

  // The current parse position within the token stream.
  size_t pos_ = 0;

  // This is set by NounState and can be read from NounIndexState.
  Noun noun_;

  // As soon as we parse a valid verb, this points to its VerbRecord.
  const VerbRecord* verb_record_ = nullptr;

  // This is set to the record for the found switch by SwitchState, and should
  // be readable from SwitchArgState.
  const SwitchRecord* sw_record_ = nullptr;

  // This is set to the text of the switch by SwitchState, and can be read from
  // SwitchArgState for error-reporting purposes.
  std::string sw_name_;

  // This is set by SwitchState when we have an argument that is in the same
  // token as the switch itself (i.e. `--foo=1` or `-f1`). It is read from
  // SwitchArgState, and if it is populated, we take the argument from there
  // instead of consuming another token.
  std::optional<std::string> sw_value_ = std::nullopt;

  std::vector<CommandToken> tokens_;

  // States we've been through without advancing. This is important for
  // completion.
  //
  // In essence this is a list of every state we've been through since the last
  // time pos_ changed. If we are completing, and thus at the end of the
  // stream, these are potentially all states that would like to have consumed
  // another token but could not, and so passed control to another state or
  // halted. Completion means going back to each of them and asking what we
  // would like to have seen.
  //
  // An example. Suppose we had just the noun token "thread". We will parse
  // that from the noun state, then enter the NounIndex state, which will
  // move us back to the Noun state without parsing anything, and so will be
  // added to this list. The Noun state will find no further tokens and thus
  // pass control to the Verb state, and thus also get added to this list. The
  // verb state will then accept without parsing, and it too will be added to
  // this list, so at halt we will have the Noun, NounIndex, and Verb states,
  // and on completion we will try to complete with either a Noun, an index,
  // or a Verb.
  //
  // It's important to note that any time the parser advances, this list is
  // cleared, so we end with only states that tried to match at the end but
  // found no token.
  std::vector<const State*> states_at_pos_;
};

const Parser::State Parser::kNounState(&Parser::DoNounState, &Parser::DoCompleteNoun);
const Parser::State Parser::kNounIndexState(&Parser::DoNounIndexState, &Parser::NoComplete);
const Parser::State Parser::kVerbState(&Parser::DoVerbState, &Parser::DoCompleteVerb);
const Parser::State Parser::kSwitchesState(&Parser::DoSwitchesState, &Parser::DoCompleteSwitches);
const Parser::State Parser::kSwitchState(&Parser::DoSwitchState, &Parser::NoComplete);
const Parser::State Parser::kLongSwitchState(&Parser::DoLongSwitchState, &Parser::NoComplete);
const Parser::State Parser::kSwitchArgState(&Parser::DoSwitchArgState, &Parser::NoComplete);
const Parser::State Parser::kArgState(&Parser::DoArgState, &Parser::DoCompleteArgs);

bool Parser::DoNounState() {
  if (at_end()) {
    return GoTo(Parser::kVerbState);
  }

  const auto& nouns = GetStringNounMap();
  auto found = nouns.find(token_str());
  if (found == nouns.end()) {
    if (pos_ > 0 && !at_end() && token_str()[0] == '-') {
      return GoTo(Parser::kSwitchesState);
    } else {
      return GoTo(Parser::kVerbState);
    }
  }

  noun_ = found->second;
  if (command_->HasNoun(noun_)) {
    return Fail("Noun \"" + NounToString(noun_) + "\" specified twice.");
  }

  return Consume(Parser::kNounIndexState);
}

// Consume optional following index if it's all integers. For example, it
// could be "process 2 run" (with index) or "process run" (without).
bool Parser::DoNounIndexState() {
  if (at_end() || !IsIndexToken(token_str())) {
    command_->SetNoun(noun_, Command::kNoIndex);
    return GoTo(Parser::kNounState);
  }

  size_t noun_index = Command::kNoIndex;
  if (sscanf(token_str().c_str(), "%zu", &noun_index) != 1) {
    return Fail("Invalid index \"" + token_str() + "\" for \"" + NounToString(noun_) + "\".");
  }

  command_->SetNoun(noun_, noun_index);
  return Consume(Parser::kNounState);
}

bool Parser::DoSwitchesState() {
  if (at_end()) {
    return GoTo(Parser::kArgState);
  }

  if (token_str()[0] != '-') {
    return GoTo(Parser::kArgState);
  }

  if (token_str() == "--") {
    return Consume(Parser::kArgState);
  }

  if (token_str().size() == 1) {
    return Fail("Invalid switch \"-\".");
  }

  if (token_str()[1] == '-') {
    return GoTo(Parser::kLongSwitchState);
  } else {
    return GoTo(Parser::kSwitchState);
  }
}

bool Parser::DoLongSwitchState() {
  const std::vector<SwitchRecord>& switches =
      verb_record_ ? verb_record_->switches : GetNounSwitches();

  // Two-hyphen (--) switch.
  size_t equals_index = std::string::npos;
  sw_record_ = FindLongSwitch(token_str(), switches, &equals_index);
  if (!sw_record_) {
    return Fail("Unknown switch \"" + token_str() + "\".");
  }

  sw_name_ = std::string("--") + sw_record_->name;

  if (equals_index != std::string::npos) {
    // Extract the token following the equals sign.
    sw_value_ = token_str().substr(equals_index + 1);
  } else {
    sw_value_ = std::nullopt;
  }

  return Consume(Parser::kSwitchArgState);
}

bool Parser::DoSwitchState() {
  const std::vector<SwitchRecord>& switches =
      verb_record_ ? verb_record_->switches : GetNounSwitches();
  std::string value;

  // Single-dash token means one character.
  char switch_char = token_str()[1];
  sw_record_ = FindSwitch(switch_char, switches);
  if (!sw_record_) {
    return Fail(std::string("Unknown switch \"-") + switch_char + "\".");
  }

  sw_name_ = std::string("-") + sw_record_->ch;

  if (token_str().size() > 2) {
    sw_value_ = token_str().substr(2);
  }

  return Consume(Parser::kSwitchArgState);
}

bool Parser::DoSwitchArgState() {
  if (!sw_record_->has_value && !sw_value_) {
    command_->SetSwitch(sw_record_->id, "");
    return GoTo(Parser::kSwitchesState);
  }

  if (!sw_record_->has_value) {
    return Fail(std::string("--") + sw_record_->name + " takes no argument.");
  }

  if (sw_value_) {
    command_->SetSwitch(sw_record_->id, std::move(*sw_value_));
    return GoTo(Parser::kSwitchesState);
  } else if (at_end()) {
    return Fail("Argument needed for \"" + sw_name_ + "\".");
  } else {
    command_->SetSwitch(sw_record_->id, token_str());
    return Consume(Parser::kSwitchesState);
  }
}

bool Parser::DoVerbState() {
  if (at_end()) {
    return Accept();
  }

  // Consume the verb.
  const auto& verb_strings = GetStringVerbMap();
  auto found_verb_str = verb_strings.find(token_str());
  if (found_verb_str == verb_strings.end()) {
    return Fail("The string \"" + token_str() + "\" is not a valid verb.");
  }

  command_->set_verb(found_verb_str->second);

  // Find the verb record.
  const auto& verbs = GetVerbs();
  auto found_verb = verbs.find(command_->verb());
  FXL_DCHECK(found_verb != verbs.end());  // Valid verb should always be found.
  verb_record_ = &found_verb->second;

  return Consume(Parser::kSwitchesState);
}

bool Parser::DoArgState() {
  if (pos_ == tokens_.size()) {
    return Accept();
  }

  std::vector<std::string> args;
  if (verb_record_ && verb_record_->param_type == VerbRecord::kOneParam) {
    // Treat all arguments as one giant parameter rather than whitespace separated ones.
    args.push_back(input_.substr(tokens_[pos_].offset));
  } else {
    for (size_t i = pos_; i < tokens_.size(); i++)
      args.push_back(tokens_[i].str);
  }

  command_->set_args(std::move(args));
  pos_ = tokens_.size();

  return Accept();
}

void Parser::DoCompleteNoun(const std::string& to_complete, std::vector<std::string>* result) {
  for (const auto& noun_pair : GetNouns()) {
    if (command_->HasNoun(noun_pair.first)) {
      continue;
    }

    for (size_t i = 0; i < noun_pair.second.aliases.size(); i++) {
      std::string noun_name = noun_pair.second.aliases[i];

      if (StartsWith(noun_name, to_complete)) {
        result->push_back(noun_name);
        break;
      }
    }
  }
}

void Parser::DoCompleteSwitches(const std::string& to_complete, std::vector<std::string>* result) {
  const std::vector<SwitchRecord>& switches =
      verb_record_ ? verb_record_->switches : GetNounSwitches();

  for (const auto& sw : switches) {
    std::string long_name = "--";
    long_name += sw.name;

    if (StartsWith(long_name, to_complete)) {
      result->push_back(long_name);
    }
  }
}

void Parser::DoCompleteVerb(const std::string& to_complete, std::vector<std::string>* result) {
  if (verb_record_) {
    return;
  }

  for (const auto& verb_pair : GetVerbs()) {
    for (size_t i = 0; i < verb_pair.second.aliases.size(); i++) {
      std::string verb_name = verb_pair.second.aliases[i];

      if (StartsWith(verb_name, to_complete)) {
        result->push_back(verb_name);
        break;
      }
    }
  }
}

void Parser::DoCompleteArgs(const std::string& to_complete, std::vector<std::string>* result) {
  if (verb_record_ && verb_record_->complete) {
    // Fill in the noun context if possible for the completion routine.
    if (fill_context_)
      fill_context_(command_);
    verb_record_->complete(*command_, to_complete, result);
  }
}

}  // namespace

Err TokenizeCommand(const std::string& input, std::vector<CommandToken>* result) {
  result->clear();

  // TODO(brettw) this will probably need some kind of quoting and escaping logic.
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
    result->emplace_back(token_begin, input.substr(token_begin, cur - token_begin));
  }

  // This returns an Err() to allow for adding escaping errors in the future.
  return Err();
}

Err ParseCommand(const std::string& input, Command* output) {
  *output = Command();

  Parser parser(input, output, FillCommandContextCallback());
  parser.Parse();

  FXL_DCHECK(parser.err().has_error() || parser.at_end());

  return parser.err();
}

// It would be nice to do more context-aware completions. For now, just
// complete based on all known nouns and verbs.
std::vector<std::string> GetCommandCompletions(const std::string& input,
                                               const FillCommandContextCallback& fill_context) {
  Command temp;
  Parser parser(input, &temp, std::move(fill_context));

  std::vector<std::string> result;
  parser.Complete(&result);

  return result;
}

}  // namespace zxdb
