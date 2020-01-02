// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fidlcat {

void Comparator::CompareInput(std::string_view syscall_inputs) {
  // A difference has already been found, no need for anymore comparison.
  if (found_difference_) {
    return;
  }
  std::string_view expected_inputs = GetNextExpectedMessage();
  std::string actual_inputs = syscall_inputs.length() > 0 && syscall_inputs[0] == '\n'
                                  ? static_cast<std::string>(syscall_inputs.substr(1))
                                  : static_cast<std::string>(syscall_inputs);
  if (!SameProcessNamePidTid(&actual_inputs, expected_inputs)) {
    found_difference_ = true;
    return;
  }

  // Now we replace handles ids, then compare.
  static std::vector<std::string> handle_texts = {"handle: ", "handle = "};
  for (size_t i = 0; i < handle_texts.size(); i++) {
    if (!CouldReplaceHandles(&actual_inputs, expected_inputs, handle_texts[i])) {
      found_difference_ = true;
      return;
    }
  }

  if (actual_inputs.compare(expected_inputs) != 0) {
    compare_results_ << "Different messages. expected " << expected_inputs << " got "
                     << actual_inputs;
    found_difference_ = true;
    return;
  }
}

void Comparator::CompareOutput(std::string_view syscall_outputs) {
  // A difference has already been found, no need for anymore comparison.
  if (found_difference_) {
    return;
  }
  std::string actual_outputs = syscall_outputs.length() > 0 && syscall_outputs[0] == '\n'
                                   ? static_cast<std::string>(syscall_outputs.substr(1))
                                   : static_cast<std::string>(syscall_outputs);

  std::string_view expected_outputs = GetNextExpectedMessage();
  // Does output begin with process name, pid and tid?
  if (actual_outputs.find("  ->") != 0) {
    if (!SameProcessNamePidTid(&actual_outputs, expected_outputs)) {
      found_difference_ = true;
      return;
    }
  }
  // Now we replace handle ids, then compare.
  static std::vector<std::string> handle_texts = {"handle: ", "handle = "};
  for (size_t i = 0; i < handle_texts.size(); i++) {
    if (!CouldReplaceHandles(&actual_outputs, expected_outputs, handle_texts[i])) {
      found_difference_ = true;
      return;
    }
  }

  if (actual_outputs.compare(expected_outputs) != 0) {
    compare_results_ << "Different messages, expected " << expected_outputs << " got "
                     << actual_outputs;
    found_difference_ = true;
    return;
  }
}
void Comparator::DecodingError(std::string error) {
  found_difference_ = true;
  compare_results_ << "Unexpected decoding error in the current execution:\n" << error;
}

bool Comparator::SameProcessNamePidTid(std::string* actual, std::string_view expected) {
  size_t actual_name_position = actual->find(' ');
  size_t expected_name_position = expected.find(' ');
  if (expected_name_position != actual_name_position ||
      expected.substr(0, expected_name_position).compare(actual->substr(0, actual_name_position)) !=
          0) {
    compare_results_ << "Different process names, expected "
                     << expected.substr(0, expected_name_position) << " actual was "
                     << actual->substr(0, actual_name_position);
    return false;
  }
  // Replacing pid and tid.
  uint64_t actual_pid = ExtractUint64(actual->substr(actual_name_position + 1));
  uint64_t expected_pid = ExtractUint64(expected.substr(expected_name_position + 1));
  uint64_t actual_tid = ExtractUint64(actual->substr(actual->find(':', actual_name_position) + 1));
  uint64_t expected_tid =
      ExtractUint64(expected.substr(expected.find(':', expected_name_position) + 1));

  // The actual pid matches the expected pid
  if (expected_pids_.find(actual_pid) == expected_pids_.end()) {
    expected_pids_[actual_pid] = expected_pid;
  } else if (expected_pids_[actual_pid] != expected_pid) {
    compare_results_ << "Different pids, actual pid " << actual_pid << " should be "
                     << expected_pids_[actual_pid] << " in golden file but was " << expected_pid;
    return false;
  }
  // and no other actual pid does.
  if (actual_pids_.find(expected_pid) == actual_pids_.end()) {
    actual_pids_[expected_pid] = actual_pid;
  } else if (actual_pids_[expected_pid] != actual_pid) {
    compare_results_ << "Different pids, expected pid " << expected_pid << " should be "
                     << actual_pids_[expected_pid] << " in this execution but was " << actual_pid;
    return false;
  }

  // The actual pid:tid matches the expected pid:tid
  if (expected_pids_tids_.find(std::make_pair(actual_pid, actual_tid)) ==
      expected_pids_tids_.end()) {
    expected_pids_tids_[std::make_pair(actual_pid, actual_tid)] =
        std::make_pair(expected_pid, expected_tid);
  } else if (expected_pids_tids_[std::make_pair(actual_pid, actual_tid)] !=
             std::make_pair(expected_pid, expected_tid)) {
    compare_results_ << "Different tids, actual pid:tid " << actual_pid << ":" << actual_tid
                     << " should be "
                     << expected_pids_tids_[std::make_pair(actual_pid, actual_tid)].first << ":"
                     << expected_pids_tids_[std::make_pair(actual_pid, actual_tid)].second
                     << " in golden file but was " << expected_pid << ":" << expected_tid;
    return false;
  }
  // and no other actual pid:tid does.
  if (actual_pids_tids.find(std::make_pair(expected_pid, expected_tid)) == actual_pids_tids.end()) {
    actual_pids_tids[std::make_pair(expected_pid, expected_tid)] =
        std::make_pair(actual_pid, actual_tid);
  } else if (actual_pids_tids[std::make_pair(expected_pid, expected_tid)] !=
             std::make_pair(actual_pid, actual_tid)) {
    compare_results_ << "Different tids, expected pid:tid " << expected_pid << ":" << expected_tid
                     << " should be "
                     << actual_pids_tids[std::make_pair(expected_pid, expected_tid)].first << ":"
                     << actual_pids_tids[std::make_pair(expected_pid, expected_tid)].second
                     << " in this execution but was " << actual_pid << ":" << actual_tid;
    return false;
  }
  actual->replace(actual_name_position,
                  actual->find(' ', actual_name_position + 1) - actual_name_position, expected,
                  expected_name_position,
                  expected.find(' ', expected_name_position + 1) - expected_name_position);
  return true;
}

bool Comparator::CouldReplaceHandles(std::string* actual, std::string_view expected,
                                     std::string_view handle_text) {
  size_t handle_position = actual->find(handle_text);
  while (handle_position != std::string::npos) {
    if (expected.find(handle_text, handle_position) != handle_position) {
      compare_results_ << "Different messages, expected " << expected << " actual " << actual;
      return false;
    }
    handle_position += handle_text.length();
    uint32_t act_handle = ExtractHexUint32(std::string_view(*actual).substr(handle_position));
    uint32_t exp_handle = ExtractHexUint32(expected.substr(handle_position));

    // Actual handle matches to the expected
    if (expected_handles_.find(act_handle) == expected_handles_.end()) {
      expected_handles_[act_handle] = exp_handle;
    } else if (expected_handles_[act_handle] != exp_handle) {
      compare_results_ << std::hex;
      compare_results_ << "Different handles, actual handle " << act_handle << " should be "
                       << expected_handles_[act_handle] << " in golden file but was " << exp_handle;
      compare_results_ << std::dec;
      return false;
    }

    // and no other actual handle matches to the same expected.
    if (actual_handles_.find(exp_handle) == actual_handles_.end()) {
      actual_handles_[exp_handle] = act_handle;
    } else if (actual_handles_[exp_handle] != act_handle) {
      compare_results_ << std::hex;
      compare_results_ << "Different handles, expected handle " << exp_handle << " should be "
                       << actual_handles_[exp_handle] << " in this execution but was "
                       << act_handle;
      compare_results_ << std::dec;
      return false;
    }
    size_t act_handle_end = actual->find_first_not_of("abcdef1234567890", handle_position);
    size_t exp_handle_end = expected.find_first_not_of("abcdef1234567890", handle_position);
    actual->replace(handle_position, act_handle_end - handle_position,
                    expected.substr(handle_position, exp_handle_end - handle_position));
    handle_position = actual->find(handle_text, handle_position);
  }
  return true;
}

uint64_t Comparator::ExtractUint64(std::string_view str) {
  uint64_t result = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char value = str[i];
    if ((value < '0') || (value > '9')) {
      break;
    }
    result = 10 * result + (value - '0');
  }
  return result;
}

uint32_t Comparator::ExtractHexUint32(std::string_view str) {
  uint32_t result = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char value = str[i];
    if ('0' <= value && value <= '9') {
      result = 16 * result + (value - '0');
    } else if ('a' <= value && value <= 'f') {
      result = 16 * result + (value - 'a' + 10);
    } else {
      break;
    }
  }
  return result;
}

std::string_view Comparator::GetNextExpectedMessage() {
  // begin points to the beginning of the line, end to its end.
  size_t begin = position_in_golden_file_;
  size_t end = expected_output_.find('\n', begin);
  // Ignore fidlcat startup lines or empty lines.
  while (end != std::string::npos) {
    if (!IgnoredLine(expected_output_.substr(begin, end - begin))) {
      break;
    }
    begin = end + 1;
    end = expected_output_.find('\n', begin);
  }
  // Now we get the message.
  size_t bpos = begin;
  while (end != std::string::npos) {
    // New line indicates end of syscall input or output.
    if (bpos < begin && end == begin && expected_output_[begin] == '\n') {
      break;
    }
    // Beginning of syscall output display.
    if (bpos < begin && expected_output_.substr(begin, end - begin).find("  ->") == 0) {
      break;
    }
    begin = end + 1;
    end = expected_output_.find('\n', begin);
  }
  position_in_golden_file_ = begin;

  // Note that as expected_output_ is a string_view, substr() returns a string_view as well.
  return expected_output_.substr(bpos, begin - bpos);
}

bool Comparator::IgnoredLine(std::string_view line) {
  if (line.length() == 0) {
    return true;
  }
  if (line.length() == 1 && line[0] == '\n') {
    return true;
  }
  static std::vector<std::string> to_be_ignored = {"Checking", "Debug", "Launched", "Monitoring"};
  for (size_t i = 0; i < to_be_ignored.size(); i++) {
    if (line.find(to_be_ignored[i]) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace fidlcat
