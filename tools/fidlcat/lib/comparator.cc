// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fidlcat {

void Comparator::CompareInput(std::string_view syscall_inputs, uint64_t actual_pid,
                              uint64_t actual_tid) {
  // A difference has already been found, no need for anymore comparing.
  if (found_difference_) {
    return;
  }

  std::unique_ptr<Message> expected_inputs = GetNextExpectedMessage(actual_pid, actual_tid);
  if (!expected_inputs) {
    found_difference_ = true;
    return;
  }

  // Now get process name, and remove the header from syscall_inputs.
  std::string_view actual_process_name =
      syscall_inputs.length() > 0 && syscall_inputs[0] == '\n'
          ? syscall_inputs.substr(1, syscall_inputs.find(' ') - 1)
          : syscall_inputs.substr(0, syscall_inputs.find(' '));
  if (actual_process_name.compare(expected_inputs->process_name) != 0) {
    found_difference_ = true;
    compare_results_ << "Different process names for actual pid:tid " << actual_pid << ":"
                     << actual_tid << ", matched with expected pid:tid "
                     << expected_pids_[actual_pid] << ":"
                     << expected_pids_tids_[std::make_pair(actual_pid, actual_tid)].second
                     << ", expected " << expected_inputs->process_name << " actual was "
                     << actual_process_name;
    return;
  }
  std::string actual_inputs = static_cast<std::string>(
      syscall_inputs.substr(syscall_inputs.find(' ', syscall_inputs.find(":")) + 1));

  // Now we replace handles ids, then compare.
  static std::vector<std::string> handle_texts = {"handle: ", "handle = "};
  for (size_t i = 0; i < handle_texts.size(); i++) {
    if (!CouldReplaceHandles(&actual_inputs, expected_inputs->message, handle_texts[i])) {
      found_difference_ = true;
      return;
    }
  }

  if (actual_inputs.compare(expected_inputs->message) != 0) {
    compare_results_ << "Different messages. expected " << expected_inputs->message << " got "
                     << actual_inputs;
    found_difference_ = true;
    return;
  }
}

void Comparator::CompareOutput(std::string_view syscall_outputs, uint64_t actual_pid,
                               uint64_t actual_tid) {
  // A difference has already been found, no need for anymore comparison.
  if (found_difference_) {
    return;
  }

  std::unique_ptr<Message> expected_outputs = GetNextExpectedMessage(actual_pid, actual_tid);
  if (!expected_outputs) {
    found_difference_ = true;
    return;
  }

  // Does output begin with process name, pid and tid?
  std::string actual_outputs;
  //'process_name pid:tid ' with one char for process_name/pid/tid
  constexpr size_t kMinNbCharHeader = 5;
  if (syscall_outputs.find("->") > kMinNbCharHeader) {
    // We check that processes names match.
    std::string_view actual_process_name =
        syscall_outputs.length() > 0 && syscall_outputs[0] == '\n'
            ? syscall_outputs.substr(1, syscall_outputs.find(' ') - 1)
            : syscall_outputs.substr(0, syscall_outputs.find(' '));
    if (actual_process_name.compare(expected_outputs->process_name) != 0) {
      found_difference_ = true;
      compare_results_ << "Different process names for actual pid:tid " << actual_pid << ":"
                       << actual_tid << ", matched with expected pid:tid "
                       << expected_pids_[actual_pid] << ":"
                       << expected_pids_tids_[std::make_pair(actual_pid, actual_tid)].second
                       << ", expected " << expected_outputs->process_name << " actual was "
                       << actual_process_name;
      return;
    }
    actual_outputs = static_cast<std::string>(
        syscall_outputs.substr(syscall_outputs.find(' ', syscall_outputs.find(":")) + 1));
  } else {
    actual_outputs = syscall_outputs.length() > 0 && syscall_outputs[0] == '\n'
                         ? static_cast<std::string>(syscall_outputs.substr(1))
                         : static_cast<std::string>(syscall_outputs);
  }
  // Now we replace handle ids, then compare.
  static std::vector<std::string> handle_texts = {"handle: ", "handle = "};
  for (size_t i = 0; i < handle_texts.size(); i++) {
    if (!CouldReplaceHandles(&actual_outputs, expected_outputs->message, handle_texts[i])) {
      found_difference_ = true;
      return;
    }
  }

  if (actual_outputs.compare(expected_outputs->message) != 0) {
    compare_results_ << "Different messages, expected " << expected_outputs->message << " got "
                     << actual_outputs;
    found_difference_ = true;
    return;
  }
}
void Comparator::DecodingError(std::string error) {
  found_difference_ = true;
  compare_results_ << "Unexpected decoding error in the current execution:\n" << error;
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

std::unique_ptr<Message> Comparator::GetNextExpectedMessage(uint64_t actual_pid,
                                                            uint64_t actual_tid) {
  uint64_t expected_pid = 0;
  uint64_t expected_tid = 0;

  // Have we already seen this pid?
  if (expected_pids_.find(actual_pid) == expected_pids_.end()) {
    if (pids_by_order_of_appearance_.empty()) {
      compare_results_ << "More actual processes than expected.";
      return nullptr;
    }
    // We assume the order of appearance of pids are the same in the golden file and in the current
    // execution.
    expected_pid = pids_by_order_of_appearance_.front();
    pids_by_order_of_appearance_.pop_front();
    expected_pids_[actual_pid] = expected_pid;
    // As each expected pid appears only once in pids_by_order_of_appearance_, we know for sure that
    // only this actual_pid can link to this expected_pid, no need for a reverse map.
  } else {
    expected_pid = expected_pids_[actual_pid];
  }

  // Have we already seen this tid?
  if (expected_pids_tids_.find(std::make_pair(actual_pid, actual_tid)) ==
      expected_pids_tids_.end()) {
    expected_pid = expected_pids_[actual_pid];
    if (tids_by_order_of_appearance_[expected_pid].empty()) {
      compare_results_ << "More actual threads than expected for actual pid " << actual_pid
                       << " matched with expected pid " << expected_pid;
      return nullptr;
    }
    // We assume the order of appearance of tids per pid are the same in the golden file and in the
    // current execution.
    expected_tid = tids_by_order_of_appearance_[expected_pid].front();
    tids_by_order_of_appearance_[expected_pid].pop_front();
    expected_pids_tids_[std::make_pair(actual_pid, actual_tid)] =
        std::make_pair(expected_pid, expected_tid);
    // Same as above, no need for a reverse map.
  } else {
    expected_tid = expected_pids_tids_[std::make_pair(actual_pid, actual_tid)].second;
  }

  if (messages_[std::make_pair(expected_pid, expected_tid)].empty()) {
    compare_results_ << "More actual messages than expected for actual pid:tid " << actual_pid
                     << ":" << actual_tid << " matched with expected pid:tid " << expected_pid
                     << ":" << expected_tid;
    return nullptr;
  }
  std::unique_ptr<Message> expected_message =
      std::move(messages_[std::make_pair(expected_pid, expected_tid)].front());
  messages_[std::make_pair(expected_pid, expected_tid)].pop_front();
  return expected_message;
}

void Comparator::ParseGolden(std::string_view golden_file_contents) {
  size_t processed_char_count = 0;

  std::string_view cur_msg = GetMessage(golden_file_contents, &processed_char_count);
  uint64_t previous_pid = 0;
  uint64_t previous_tid = 0;
  std::string_view previous_process_name;
  while (!cur_msg.empty()) {
    uint64_t pid, tid;
    std::string_view process_name;
    //'process_name pid:tid ' with one char for process_name/pid/tid
    constexpr size_t kMinNbCharHeader = 5;

    // The message does not contain the process name and pid:tid.
    if (cur_msg.find("->") <= kMinNbCharHeader) {
      pid = previous_pid;
      tid = previous_tid;
      process_name = previous_process_name;
    } else {
      pid = ExtractUint64(cur_msg.substr(cur_msg.find(' ') + 1));
      tid = ExtractUint64(cur_msg.substr(cur_msg.find(':') + 1));
      process_name = cur_msg.substr(0, cur_msg.find(' '));
      // We remove process name, pid and tid from the message, as those are now saved in the map
      // explicitely
      cur_msg = cur_msg.substr(cur_msg.find(' ', cur_msg.find(':')) + 1);
    }

    // Have we already seen this pid?
    if (tids_by_order_of_appearance_.find(pid) == tids_by_order_of_appearance_.end()) {
      std::deque<uint64_t> tids = {tid};
      tids_by_order_of_appearance_[pid] = tids;
      pids_by_order_of_appearance_.push_back(pid);
      messages_[std::make_pair(pid, tid)] = std::deque<std::unique_ptr<Message>>();
    }

    // Have we already seen this tid?
    else if (messages_.find(std::make_pair(pid, tid)) == messages_.end()) {
      tids_by_order_of_appearance_[pid].push_back(tid);
      messages_[std::make_pair(pid, tid)] = std::deque<std::unique_ptr<Message>>();
    }
    messages_[std::make_pair(pid, tid)].push_back(std::make_unique<Message>(process_name, cur_msg));
    golden_file_contents = golden_file_contents.substr(processed_char_count);
    cur_msg = GetMessage(golden_file_contents, &processed_char_count);
    previous_pid = pid;
    previous_tid = tid;
    previous_process_name = process_name;
  }
}

std::string_view Comparator::GetMessage(std::string_view messages, size_t* processed_char_count) {
  // begin points to the beginning of the line, end to its end.
  size_t begin = 0;
  size_t end = messages.find('\n', begin);
  // Ignore fidlcat startup lines or empty lines.
  while (end != std::string::npos) {
    if (!IgnoredLine(messages.substr(begin, end - begin))) {
      break;
    }
    begin = end + 1;
    end = messages.find('\n', begin);
  }

  // Now we get the message.
  size_t bpos = begin;
  while (end != std::string::npos) {
    // New line indicates end of syscall input or output.
    if (bpos < begin && end == begin && messages[begin] == '\n') {
      break;
    }
    // Beginning of syscall output display.
    if (bpos < begin && messages.substr(begin, end - begin).find("  ->") == 0) {
      break;
    }
    begin = end + 1;
    end = messages.find('\n', begin);
  }
  *processed_char_count = begin;

  // Note that as expected_output_ is a string_view, substr() returns a string_view as well.
  return messages.substr(bpos, begin - bpos);
}

bool Comparator::IgnoredLine(std::string_view line) {
  if (line.length() == 0) {
    return true;
  }
  if (line.length() == 1 && line[0] == '\n') {
    return true;
  }
  static std::vector<std::string> to_be_ignored = {"Checking", "Debug", "Launched", "Monitoring",
                                                   "Stop"};
  for (size_t i = 0; i < to_be_ignored.size(); i++) {
    if (line.find(to_be_ignored[i]) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace fidlcat
