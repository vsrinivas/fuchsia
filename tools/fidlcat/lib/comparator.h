// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_COMPARATOR_H_
#define TOOLS_FIDLCAT_LIB_COMPARATOR_H_

#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "message_graph.h"

namespace fidlcat {

// To compare the messages stored in the golden file to the messages intercepted by fidlcat in the
// current execution, this class first builds a GoldenMessageGraph from the golden file when
// initialized. It also creates an empty ActualMessageGraph, actual_message_graph_, for the current
// execution. Then for each message passed to CompareInput or CompareOutput, it updates
// actual_message_graph_ by inserting the new message in it. If this message can be matched uniquely
// to a message from the golden_message_graph_, we record it, and try to propagate this matching
// along dependenies in the graphs. When there are no more messages to receive, that is to say when
// FinishComparison is called, we propagate (along dependencies) and reverse propagate
// (along reverse dependencies) matchings for all nodes.
class Comparator {
 public:
  Comparator(std::string_view compare_file_name, std::ostream& os) : compare_results_(os) {
    std::string golden_file_contents;
    std::ifstream compare_file(compare_file_name);
    golden_file_contents.assign((std::istreambuf_iterator<char>(compare_file)),
                                (std::istreambuf_iterator<char>()));
    ParseGolden(golden_file_contents);
  }

  // Creates a new node in actual_message_graph_ and tries to match.
  void CompareInput(std::string_view syscall_inputs, std::string_view actual_process_name,
                    uint64_t actual_pid, uint64_t actual_tid);

  void CompareOutput(std::string_view syscall_outputs, std::string_view actual_process_name,
                     uint64_t actual_pid, uint64_t actual_tid);

  // As the golden file should not contain any error, any error in the actual execution results in
  // an error message.
  void DecodingError(std::string_view error);

  // Tries, using all the information in golden_message_graph_ and actual_message_graph_, to match
  // as many nodes as possible to one another, and outputs the result of the comparison.
  void FinishComparison();

 protected:
  // For testing, expected_output_ should then be set using ParseGolden.
  Comparator(std::ostream& os) : compare_results_(os) {}

  // Given a message node for the current execution, see if it can be uniquely matched with a
  // golden message node. Returns true if and only if there is exactly one golden node that could
  // match, and false otherwise. In case no golden node could match this message, outputs an error
  // to compare_results_.
  bool UniqueMatchToGolden(std::shared_ptr<ActualMessageNode> actual_message_node);

  // Given an actual node with a matching golden node (ie assumes
  // actual_node->matching_golden_node()) is not null) recursively propagates this matching
  // along all dependency links. Returns false if an inconsistency in the matching was found while
  // propagating. If reverse_propagate is set to true, also runs ReversePropagate for any new
  // matching found.
  bool PropagateMatch(std::shared_ptr<ActualNode> actual_node, bool reverse_propagate);

  // Given an actual node with a matching golden node (ie assumes
  // actual_node->matching_golden_node()) is not null) recursively propagates this matching
  // along reverse dependency links. Returns false if an inconsistency in the matching was found
  // while propagating. Also runs Propagate for any new matching found. Assumes the
  // actual_message_graph_ is complete, that is to say no more messages/nodes/links will be added to
  // it.
  bool ReversePropagateMatch(std::shared_ptr<ActualNode> actual_node);

  // Creates the golden_message_graph_ from the contents of the file.
  void ParseGolden(std::string_view golden_file_contents);

  // Returns the first block of syscall input or output from messages, and stores the number of
  // characters processed in processed_char_count (which may be different from the length of the
  // message if some lines from messages were ignored).
  static std::string_view GetMessage(std::string_view messages, size_t* processed_char_count);

  // golden_message_graph contains all the information about the execution saved in the golden file,
  // while acutal_message_graph is constructed progressively, every time fidlcat intercepts a
  // message in the current execution.
  GoldenMessageGraph golden_message_graph_;
  ActualMessageGraph actual_message_graph_;

  // We need this map to link output messages to their corresponding input messages.
  std::map<uint64_t, std::shared_ptr<ActualMessageNode>> last_unmatched_input_from_tid_;

 private:
  std::ostream& compare_results_;

  // Returns true if line is the final line of a multiline request/response (ie begins with the
  // proper number of spaces as indentation, then a sequence of " }" or " ]") We rely here on
  // fidl_codec printing: it indents all the contents of a message by at least (indentation of
  // beginning of message + 1), and the last line of the message will be a line containing exactly
  // the same indentation as the beginning of the message and one or more closing brackets.
  static bool ClosingSequence(std::string_view line, size_t indentation);

  // Returns true if line is not part of a message (ie a fidlcat startup indication or a newline).
  static bool IgnoredLine(std::string_view line);

  // Expects decimal numbers.
  static uint64_t ExtractUint64(std::string_view str);

  // Removes the header (process name pid:tid) from a message. If there is no header, returns the
  // string unchanged. If pid, tid and process_name pointers are passed as arguments, updates them
  // if a header was present, leaves them unchanged otherwise.
  static std::string_view AnalyzesAndRemovesHeader(std::string_view message,
                                                   std::string* process_name = nullptr,
                                                   uint64_t* pid = nullptr,
                                                   uint64_t* tid = nullptr);

  // Returns true if message is the input message of a syscall with no return value.
  static bool HasReturn(std::string_view message);
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_COMPARATOR_H_
