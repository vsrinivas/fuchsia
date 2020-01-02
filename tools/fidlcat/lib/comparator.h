// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_COMPARATOR_H_
#define TOOLS_FIDLCAT_LIB_COMPARATOR_H_

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fidlcat {

class Comparator {
 public:
  Comparator(std::string_view compare_file_name, std::ostream& os)
      : position_in_golden_file_(0), compare_results_(os), found_difference_(false) {
    std::ifstream compare_file(compare_file_name);
    golden_file_content_.assign((std::istreambuf_iterator<char>(compare_file)),
                                (std::istreambuf_iterator<char>()));
    expected_output_ = golden_file_content_;
  }

  // If no difference has been found yet (as saved in found_difference_), compares this input to
  // the current one in expected_output_ (ie the one at pos position_in_golden_file_), outputs any
  // difference to compare_results and updates found_difference_.
  void CompareInput(std::string_view syscall_inputs);

  void CompareOutput(std::string_view syscall_outputs);

  // As the golden file should not contain any error, any error in the actual execution sets
  // found_difference_ to true and displays the decoding error.
  void DecodingError(std::string error);

 protected:
  // For testing, expected_output_ should then be set using SetExpectedOutput.
  Comparator(std::ostream& os)
      : position_in_golden_file_(0),
        expected_output_(""),
        compare_results_(os),
        found_difference_(false) {}

  // Assumes that the string underlying the string_view expected_output will outlive the comparator
  // object.
  void SetExpectedOutput(std::string_view expected_output) { expected_output_ = expected_output; }

  // Returns the next block of syscall input or output from expected_output_, and updates
  // position_in_golden_file_.
  std::string_view GetNextExpectedMessage();

  // Check that both messages begin with the same name, and that pid and tids match (modulo the maps
  // below), and updates actual with the pid and tid from expected.
  bool SameProcessNamePidTid(std::string* actual, std::string_view expected);

  // Check that both messages are the same modulo handle correspondance in the maps, and updates
  // actual with the handle ids from expected.
  bool CouldReplaceHandles(std::string* actual, std::string_view expected,
                           std::string_view handle_text);

 private:
  // Position of the next message to read in the golden file, used and updated by
  // GetNextExpectedMessage().
  size_t position_in_golden_file_;
  std::string golden_file_content_;
  // string_view to golden_file_content_ to avoid unexpected dangling pointers or ackward
  // conversions when using substr.
  std::string_view expected_output_;
  std::ostream& compare_results_;
  bool found_difference_;
  std::map<uint64_t, uint64_t> expected_pids_;
  std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> expected_pids_tids_;
  std::map<uint32_t, uint32_t> expected_handles_;
  // Reverse maps to make sure we do not have two different actual ids matching to the same expected
  // ids.
  std::map<uint64_t, uint64_t> actual_pids_;
  std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> actual_pids_tids;
  std::map<uint32_t, uint32_t> actual_handles_;

  // Returns true if line is not part of a message (ie a fidlcat startup indication or a newline).
  bool IgnoredLine(std::string_view line);

  // Expects decimal numbers.
  uint64_t ExtractUint64(std::string_view str);

  // Expects hexadecimal numbers.
  uint32_t ExtractHexUint32(std::string_view str);
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_COMPARATOR_H_
