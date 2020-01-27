// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <zircon/assert.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fidlcat {

void Comparator::CompareInput(std::string_view syscall_inputs, std::string_view actual_process_name,
                              uint64_t actual_pid, uint64_t actual_tid) {
  // remove header from the message
  syscall_inputs = AnalyzesAndRemovesHeader(syscall_inputs);
  auto actual_message_node = actual_message_graph_.InsertMessage(actual_process_name, actual_pid,
                                                                 actual_tid, syscall_inputs);
  // Is there a unique match for this message in the golden messages? If so, we propagate this
  // match.
  if (UniqueMatchToGolden(actual_message_node)) {
    PropagateMatch(actual_message_node, false);
  }
  last_unmatched_input_from_tid_[actual_tid] = actual_message_node;
}

void Comparator::CompareOutput(std::string_view syscall_outputs,
                               std::string_view actual_process_name, uint64_t actual_pid,
                               uint64_t actual_tid) {
  // If present, remove header from message
  syscall_outputs = AnalyzesAndRemovesHeader(syscall_outputs);

  // Create the output node, linking it to its corresponding input node if there is one.
  auto matching_input = last_unmatched_input_from_tid_.find(actual_tid);
  auto actual_message_node =
      matching_input != last_unmatched_input_from_tid_.end()
          ? actual_message_graph_.InsertMessage(actual_process_name, actual_pid, actual_tid,
                                                syscall_outputs, matching_input->second)
          : actual_message_graph_.InsertMessage(actual_process_name, actual_pid, actual_tid,
                                                syscall_outputs);

  // Is there a unique match for this message in the golden messages? If so, we propagate this
  // match.
  if (UniqueMatchToGolden(actual_message_node)) {
    PropagateMatch(actual_message_node, false);
  }
}

void Comparator::DecodingError(std::string_view error) {
  compare_results_ << "Unexpected decoding error in the current execution:\n" << error;
}

bool Comparator::UniqueMatchToGolden(std::shared_ptr<ActualMessageNode> actual_message_node) {
  auto poss_golden_messages =
      golden_message_graph_.message_nodes().find(actual_message_node->message());
  if (poss_golden_messages == golden_message_graph_.message_nodes().end()) {  // No message matched
    compare_results_ << "No golden message could match " << *actual_message_node;
    return false;
  }
  if (poss_golden_messages->second.size() == 1) {
    // exactly one message from golden matched this string
    actual_message_node->set_matching_golden_node(poss_golden_messages->second[0]);
    return true;
  }
  // More than one golden message matched
  return false;
}

bool Comparator::PropagateMatch(std::shared_ptr<ActualNode> actual_node, bool reverse_propagate) {
  auto golden_node = actual_node->matching_golden_node();
  ZX_ASSERT(golden_node != nullptr);

  for (auto i = actual_node->dependencies().begin(); i != actual_node->dependencies().end(); i++) {
    auto actual_dependency_node = i->second;
    // The golden node that should match actual_dependency_node according to the dependency links
    // of golden_node
    auto golden_dependency_node = golden_node->get_dependency_by_type(i->first);

    // golden_node does not have the dependency actual_node has, there is no possible matching
    // between the current execution and the one stored in the golden file.
    if (!golden_dependency_node) {
      compare_results_ << *actual_node << " with dependency to " << *actual_dependency_node
                       << " was matched to " << *golden_node
                       << " which does not have this dependency \n";
      return false;
    }

    if (!actual_dependency_node->matching_golden_node()) {
      actual_dependency_node->set_matching_golden_node(golden_dependency_node);
      if (!PropagateMatch(actual_dependency_node, reverse_propagate)) {
        return false;
      }
      if (reverse_propagate && !ReversePropagateMatch(actual_dependency_node)) {
        return false;
      }
    }
    // actual_dependency_node was already matched, we check that this match makes sense for the
    // current link
    else if (actual_dependency_node->matching_golden_node() != golden_dependency_node) {
      compare_results_
          << "Conflicting matches for " << *actual_node << "matched to " << *golden_node
          << "\n. Actual has dependency to " << *actual_dependency_node << " matched to "
          << *actual_dependency_node->matching_golden_node()
          << " whereas according to dependency from actual and its match it should have been "
          << *golden_dependency_node << "\n";
      return false;
    }
  }
  return true;
}

bool Comparator::ReversePropagateMatch(std::shared_ptr<ActualNode> actual_node) {
  // golden_node matches actual_node
  auto golden_node = actual_node->matching_golden_node();
  ZX_ASSERT(golden_node != nullptr);

  // We can only propagate along a reverse dependency if it is the only one of its type.
  for (auto actual_link_type_pair = actual_node->reverse_dependencies().begin();
       actual_link_type_pair != actual_node->reverse_dependencies().end();
       actual_link_type_pair++) {
    size_t actual_nb_link_of_type = actual_link_type_pair->second.size();
    if (actual_nb_link_of_type > 1) {
      // multiple links with same type, we can't do any propagation
      continue;
    }
    auto golden_link_type_pair =
        golden_node->get_reverse_dependencies_by_type(actual_link_type_pair->first);
    // This reverse link is not present in golden_node, there is no possible matching between the
    // current execution and the one stored in the golden file.
    if (golden_link_type_pair == golden_node->reverse_dependencies().end()) {
      compare_results_ << *actual_node << " with a reverse dependency of type "
                       << actual_link_type_pair->first.second << " was matched to " << *golden_node
                       << " which has no such reverse dependency \n";
      return false;
    }
    size_t golden_nb_link_of_type = golden_link_type_pair->second.size();
    // golden node also has more reverse dependencies than actual_node, this means the matching is
    // not possible as we only call ReversePropagate when the actual_message_graph_ is complete
    if (golden_nb_link_of_type > 1) {
      compare_results_ << *actual_node << " with one reverse dependency of type "
                       << actual_link_type_pair->first.second << " was matched to " << *golden_node
                       << " which has " << golden_nb_link_of_type
                       << " such reverse dependencies \n";
      return false;
    }
    auto actual_dependency_node = actual_link_type_pair->second[0].lock();
    auto golden_dependency_node = golden_link_type_pair->second[0].lock();
    if (!actual_dependency_node->matching_golden_node()) {
      actual_dependency_node->set_matching_golden_node(golden_dependency_node);
      if (!PropagateMatch(actual_dependency_node, true) ||
          !ReversePropagateMatch(actual_dependency_node)) {
        return false;
      }
    } else if (actual_dependency_node->matching_golden_node() != golden_dependency_node) {
      compare_results_
          << "Conflicting matches for " << *actual_node << "matched to " << *golden_node
          << "\n. Actual has a reverse dependency to " << *actual_dependency_node << " matched to "
          << *actual_dependency_node->matching_golden_node()
          << " whereas according to dependency from actual and its match it should have been "
          << *golden_dependency_node << "\n";
      return false;
    }
  }
  return true;
}

void Comparator::ParseGolden(std::string_view golden_file_contents) {
  size_t processed_char_count = 0;
  // We use this map to link output messages to their corresponding input messages.
  std::map<uint64_t, std::shared_ptr<GoldenMessageNode>> last_unmatched_input_from_tid_golden;

  std::string_view cur_msg = GetMessage(golden_file_contents, &processed_char_count);
  uint64_t previous_pid = 0;
  uint64_t previous_tid = 0;
  std::string previous_process_name;
  while (!cur_msg.empty()) {
    uint64_t pid = 0, tid = 0;
    std::string process_name;

    cur_msg = AnalyzesAndRemovesHeader(cur_msg, &process_name, &pid, &tid);

    // AnalyzesAndRemovesHeader did not update the values of pid, tid and process_name, as there was
    // no header to the message (or it could not be parsed).
    if (pid == 0) {
      pid = previous_pid;
      tid = previous_tid;
      process_name = previous_process_name;
    }

    auto last_unmatched = last_unmatched_input_from_tid_golden.find(tid);
    if (last_unmatched != last_unmatched_input_from_tid_golden.end()) {
      // This is an output message, with a corresponding input messge
      auto message_node = golden_message_graph_.InsertMessage(process_name, pid, tid, cur_msg,
                                                              last_unmatched->second);
      last_unmatched_input_from_tid_golden.erase(tid);
    } else {
      auto message_node = golden_message_graph_.InsertMessage(process_name, pid, tid, cur_msg);
      if (HasReturn(cur_msg)) {
        last_unmatched_input_from_tid_golden[tid] = message_node;
      }
    }

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

std::string_view Comparator::AnalyzesAndRemovesHeader(std::string_view message,
                                                      std::string* process_name, uint64_t* pid,
                                                      uint64_t* tid) {
  constexpr size_t kMinNbCharHeader = 5;
  // The message is a syscall output with no header.
  if (message.find("->") <= kMinNbCharHeader) {
    return message;
  }

  size_t pos_pid = message.find(' ');
  size_t pos_tid = message.find(':');
  // Either there is no header, or we cannot parse it so leave it as is.
  if (pos_pid == std::string::npos || pos_tid == std::string::npos) {
    return message;
  }

  // If we have pointers to pid, tid and process_name, update those.
  if (pid) {
    *pid = ExtractUint64(message.substr(pos_pid + 1));
  }
  if (tid) {
    *tid = ExtractUint64(message.substr(pos_tid + 1));
  }
  if (process_name) {
    *process_name = message.substr(0, pos_pid);
  }
  size_t end_header = message.find(' ', pos_tid);
  if (end_header != std::string::npos) {
    return message.substr(end_header + 1);
  }
  return message;
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

bool Comparator::HasReturn(std::string_view message) {
  // Only three syscalls have no return value. Besides as we removed the header from the message,
  // the syscall name is the first word of the message.
  if (message.find("zx_thread_exit") == 0 || message.find("zx_process_exit") == 0 ||
      message.find("zx_futex_wake_handle_close_thread_exit") == 0) {
    return false;
  }
  return true;
}

void Comparator::FinishComparison() {
  // All the messages have been intercepted, we now want to check our graph:
  // - First propagates matchings along reverse dependencies now that the graph is complete,
  // - Then checks if there still are unmatched nodes.

  for (auto i = actual_message_graph_.message_nodes().begin();
       i != actual_message_graph_.message_nodes().end(); i++) {
    for (size_t j = 0; j < i->second.size(); j++) {
      if (i->second[j]->matching_golden_node() && !ReversePropagateMatch(i->second[j])) {
        // The matching failed, with a proper error message
        return;
      }
    }
  }

  for (auto i = actual_message_graph_.pid_nodes().begin();
       i != actual_message_graph_.pid_nodes().end(); i++) {
    if (i->second->matching_golden_node() && !ReversePropagateMatch(i->second)) {
      return;
    }
  }

  for (auto i = actual_message_graph_.tid_nodes().begin();
       i != actual_message_graph_.tid_nodes().end(); i++) {
    if (i->second->matching_golden_node() && !ReversePropagateMatch(i->second)) {
      return;
    }
  }

  for (auto i = actual_message_graph_.handle_nodes().begin();
       i != actual_message_graph_.handle_nodes().end(); i++) {
    if (i->second->matching_golden_node() && !ReversePropagateMatch(i->second)) {
      return;
    }
  }

  // We check that all message nodes are matched to a golden node.
  bool unmatched_message = false;
  for (auto i = actual_message_graph_.message_nodes().begin();
       i != actual_message_graph_.message_nodes().end(); i++) {
    for (size_t j = 0; j < i->second.size(); j++) {
      if (!i->second[j]->matching_golden_node()) {
        compare_results_ << "Unmatched message " << i->second[j]->message();
        unmatched_message = true;
      }
    }
  }
  // There is no need to check that handles, pids and tids are matched: as all of them have at least
  // one message that depends from them, if all messages are matched, so are they.
  if (!unmatched_message) {
    compare_results_ << "Messages from the current execution matched the golden file.\n";
  }
}
}  // namespace fidlcat
