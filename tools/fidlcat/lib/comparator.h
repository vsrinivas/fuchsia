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

namespace fidlcat {

// Used when parsing the golden file, to avoid parsing the messages header again when running the
// comparison
struct Message {
 public:
  Message(std::string_view p, std::string_view m) : process_name(p), message(m) {}
  const std::string process_name;
  const std::string message;
};

class Comparator {
 public:
  Comparator(std::string_view compare_file_name, std::ostream& os) : compare_results_(os) {
    std::string golden_file_contents;
    std::ifstream compare_file(compare_file_name);
    golden_file_contents.assign((std::istreambuf_iterator<char>(compare_file)),
                                (std::istreambuf_iterator<char>()));
    ParseGolden(golden_file_contents);
  }

  // If no difference has been found yet (as saved in found_difference_), compares this input to
  // the current one in expected_output_ (ie the one at pos position_in_golden_file_), outputs any
  // difference to compare_results and updates found_difference_.
  void CompareInput(std::string_view syscall_inputs, uint64_t actual_pid, uint64_t actual_tid);

  void CompareOutput(std::string_view syscall_outputs, uint64_t actual_pid, uint64_t actual_tid);

  // As the golden file should not contain any error, any error in the actual execution sets
  // found_difference_ to true and displays the decoding error.
  void DecodingError(std::string error);

  const std::deque<uint64_t>& pids_by_order_of_appearance() const {
    return pids_by_order_of_appearance_;
  }

  const std::map<uint64_t, std::deque<uint64_t>>& tids_by_order_of_appearance() const {
    return tids_by_order_of_appearance_;
  }

  const std::map<uint64_t, uint64_t>& expected_pids() const { return expected_pids_; }

  const std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>>& expected_pids_tids()
      const {
    return expected_pids_tids_;
  }

 protected:
  // For testing, expected_output_ should then be set using ParseGolden.
  Comparator(std::ostream& os) : compare_results_(os) {}

  // Returns the next block of syscall input or output that fit this pid and tid.
  // Returns a null ppointer if no matching message can be found.
  std::unique_ptr<Message> GetNextExpectedMessage(uint64_t actual_pid, uint64_t actual_tid);

  void ParseGolden(std::string_view golden_file_contents);

  // Returns the first block of syscall input or output from messages, and stores the number of
  // characters processed in processed_char_count (which may be different from the length of the
  // message if some lines from messages were ignored).
  static std::string_view GetMessage(std::string_view messages, size_t* processed_char_count);

  // Check that both messages are the same modulo handle correspondance in the maps, and updates
  // actual with the handle ids from expected.
  bool CouldReplaceHandles(std::string* actual, std::string_view expected,
                           std::string_view handle_text);

  // All deques in this class are used as fifo: push_back and pop_front
  // Contains all the messages (a message is a syscall input or a syscall output), mapped to their
  // (pid, tid), and stored by order of appearance in the golden file
  std::map<std::pair<uint64_t, uint64_t>, std::deque<std::unique_ptr<Message>>> messages_;

 private:
  std::ostream& compare_results_;
  bool found_difference_ = false;

  // Contains the pids by order of appearance in the golden file (pids_by_order_of_appearance.pop()
  // gives the one that appeared first)
  std::deque<uint64_t> pids_by_order_of_appearance_;

  // For each pid p, contains the tids t by order of appearance of (p, t) in the golden file
  std::map<uint64_t, std::deque<uint64_t>> tids_by_order_of_appearance_;

  // Maps to match actual pids/tids to expected pids/tids. Note that we don't need a reverse
  // actual_pids_/tids map to make sure that only one expected pid/tid matches to any given actual
  // pid/tid, as this is enforced in ParseGolden, where each expected pid/tid is added only once to
  // expected_pids_/tids, thanks to the order_of_appearance queues.
  std::map<uint64_t, uint64_t> expected_pids_;
  std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> expected_pids_tids_;
  std::map<uint32_t, uint32_t> expected_handles_;
  std::map<uint32_t, uint32_t> actual_handles_;

  // Returns true if line is not part of a message (ie a fidlcat startup indication or a newline).
  static bool IgnoredLine(std::string_view line);

  // Expects decimal numbers.
  uint64_t ExtractUint64(std::string_view str);

  // Expects hexadecimal numbers.
  uint32_t ExtractHexUint32(std::string_view str);
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_COMPARATOR_H_
