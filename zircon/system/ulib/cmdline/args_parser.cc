// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>

#include <algorithm>

namespace cmdline {

namespace {

// Returns true if the argument is the special string that indicates the end
// of options.
bool IsOptionEndFlag(const char* arg) { return strcmp(arg, "--") == 0; }

// Checks if the given argument is a short option and if it is, returns the
// letter. If not, returns 0.
char GetShortOption(const char* arg, const char** value_begin) {
  if (arg[0] == '-' && arg[1] != 0 && arg[1] != '-') {
    *value_begin = &arg[2];  // Arg (if any) follows immediately.
    return arg[1];
  }
  *value_begin = nullptr;
  return 0;
}

// Checks if the given argument is a long option and returns it, not including
// the preceding "--". If it's not an option, returns the empty string. To
// differentiate args consisting of only "--" and non-options, callers should
// call IsOptionEndFlag() before this.
std::string GetLongOption(const char* arg, const char** value_begin) {
  *value_begin = nullptr;
  if (arg[0] != '-' || arg[1] != '-')
    return std::string();

  // See if there's and "=<arg>" in this flag.
  const char* equals = strchr(arg, '=');
  if (!equals)
    return &arg[2];  // No "=<arg>".

  // value_begin gets everything after the equals.
  *value_begin = &equals[1];

  // Return everything between the '--' (2 bytes) and the equals.
  return std::string(&arg[2], equals - arg - 2);
}

}  // namespace

GeneralArgsParser::GeneralArgsParser() = default;
GeneralArgsParser::~GeneralArgsParser() = default;

void GeneralArgsParser::AddGeneralSwitch(const char* long_name, const char short_name,
                                         const char* help, OnOffSwitchCallback on_switch,
                                         OnOffSwitchCallback off_switch) {
  Record& record = records_.emplace_back();
  record.long_name = long_name;
  record.short_name = short_name;
  record.help_text = help;
  record.on_switch_callback = std::move(on_switch);
  if (off_switch != nullptr) {
    record.off_switch_callback = std::move(off_switch);
  }
}

void GeneralArgsParser::AddGeneralSwitch(const char* long_name, const char short_name,
                                         const char* help, StringCallback cb) {
  Record& record = records_.emplace_back();
  record.long_name = long_name;
  record.short_name = short_name;
  record.help_text = help;
  record.string_callback = std::move(cb);
}

std::string GeneralArgsParser::GetHelp() const {
  std::vector<std::string> switches;
  for (const auto& record : records_)
    switches.push_back(record.help_text);

  std::sort(switches.begin(), switches.end());

  std::string result;
  for (const std::string& str : switches) {
    result.append(str);
    result.append("\n\n");
  }
  return result;
}

Status GeneralArgsParser::ParseGeneral(int argc, const char* const argv[],
                                       std::vector<std::string>* params) const {
  // Expect argv[0] to be the program itself.
  if (argc <= 1)
    return Status::Ok();

  int last_option_index = argc - 1;
  for (int i = 1; i < argc; i++) {
    // Non-null when we find the argument.
    const char* arg_begin = nullptr;
    bool off_switch = false;

    const Record* record = nullptr;
    if (IsOptionEndFlag(argv[i])) {
      // End of option indicator.
      last_option_index = i;
      break;
    } else if (char c = GetShortOption(argv[i], &arg_begin)) {
      // Single-letter option.
      for (const Record& cur_record : records_) {
        if (cur_record.short_name == c) {
          record = &cur_record;
          break;
        }
      }
    } else if (auto long_opt = GetLongOption(argv[i], &arg_begin); !long_opt.empty()) {
      // Long option.
      for (const Record& cur_record : records_) {
        if (cur_record.long_name == long_opt) {
          record = &cur_record;
          break;
        } else if (std::string("no") + cur_record.long_name == long_opt) {
          off_switch = true;
          record = &cur_record;
          break;
        }
      }
    } else {
      // Non-option.
      last_option_index = i - 1;
      break;
    }

    // If we get here we should have found a record for the option.
    if (!record)
      return Status::Error(std::string(argv[i]) + " is not a valid option. " +
                           invalid_option_suggestion_);

    if (NeedsArg(record)) {
      // Arguments can be already found ("-cfoo" or "--foo=bar") or they could
      // be the following parameter.
      if (!arg_begin || !*arg_begin) {
        // Argument is in the next token of the command line.
        if (i == argc - 1)
          return Status::Error(std::string(argv[i]) +
                               " expects an argument but none was given.\n\n" + record->help_text);
        i++;
        arg_begin = argv[i];
      }
    } else {
      // Don't expect an arg for this switch.
      if (arg_begin && *arg_begin) {
        // Arg points somewhere other than the end of the string when we
        // weren't expecting an arg.
        return Status::Error(
            std::string("Unexpected value for argument that doesn't take one:\n  ") + argv[i] +
            "\n\n" + record->help_text);
      }
    }

    // Execute the right callback.
    Status status = Status::Ok();
    if (off_switch) {
      if (record->off_switch_callback) {
        record->off_switch_callback();
      } else {
        status = Status::Error(std::string("--") + record->long_name +
                               " can only be turned on, not off.\n\n" + record->help_text);
      }
    } else if (record->on_switch_callback) {
      record->on_switch_callback();
    } else if (record->string_callback) {
      status = record->string_callback(arg_begin);
    }

    if (status.has_error())
      return status;
  }

  // Everything else following the parameters are the positional arguments.
  for (int i = last_option_index + 1; i < argc; i++)
    params->push_back(argv[i]);
  return Status::Ok();
}

// static
bool GeneralArgsParser::NeedsArg(const Record* record) { return !!record->string_callback; }

namespace internal {

std::vector<std::string> SplitString(const std::string& input, char delimiter) {
  std::vector<std::string> output;

  if (input.empty()) {
    return output;
  }

  if (delimiter == '\0') {
    output.push_back(input);
    return output;
  }

  size_t start = 0;
  while (start != std::string::npos) {
    // Extract a substring.
    size_t end = input.find_first_of(delimiter, start);
    std::string str;
    if (end == std::string::npos) {
      str = input.substr(start);
      start = std::string::npos;
    } else {
      str = input.substr(start, end - start);
      start = end + 1;
    }

    // Ignore empty substring.
    if (str.empty()) {
      continue;
    }

    output.push_back(str);
  }

  return output;
}

}  // namespace internal

}  // namespace cmdline
