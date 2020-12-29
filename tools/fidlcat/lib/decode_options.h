// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
#define TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_

#include <regex.h>

#include <memory>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"

enum StackLevel { kNoStack = 0, kPartialStack = 1, kFullStack = 2 };
enum class InputMode { kDevice, kFile };
enum class OutputMode { kNone, kStandard, kTextProtobuf };

class Regex {
 public:
  explicit Regex(regex_t filter) : filter_(filter) {}
  ~Regex() { regfree(&filter_); }
  FXL_DISALLOW_COPY_AND_ASSIGN(Regex);

  bool Matches(const std::string& string) {
    return regexec(&filter_, string.c_str(), 0, nullptr, 0) == 0;
  }

 private:
  regex_t filter_;
};

struct DecodeOptions {
  // True if fidlcat doesn't automatically quit.
  bool stay_alive = false;
  // Level of stack we want to decode/display.
  int stack_level = kNoStack;
  // If a syscall satisfies one of these filters, it can be displayed.
  std::vector<std::unique_ptr<Regex>> syscall_filters;
  // But it is only displayed if it doesn't satisfy any of these filters.
  std::vector<std::unique_ptr<Regex>> exclude_syscall_filters;
  // If a message method name satisfies one of these filters, it can be displayed.
  std::vector<std::unique_ptr<Regex>> message_filters;
  // But it is only displayed if it doesn't satisfy any of these filters.
  std::vector<std::unique_ptr<Regex>> exclude_message_filters;
  // If this is not empty, messages and syscalls are only displayed when a message method name
  // satisfies one of these filters.
  std::vector<std::unique_ptr<Regex>> trigger_filters;
  // Input mode.
  InputMode input_mode = InputMode::kDevice;
  // Output mode.
  OutputMode output_mode = OutputMode::kNone;
  // File name used to save the session.
  std::string save;

  bool SatisfiesMessageFilters(const std::string& name) const {
    for (const auto& filter : exclude_message_filters) {
      if (filter->Matches(name)) {
        return false;
      }
    }
    for (const auto& filter : message_filters) {
      if (filter->Matches(name)) {
        return true;
      }
    }
    return message_filters.empty();
  }

  bool IsTrigger(const std::string& name) const {
    for (const auto& filter : trigger_filters) {
      if (filter->Matches(name)) {
        return true;
      }
    }
    return false;
  }
};

#endif  // TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
