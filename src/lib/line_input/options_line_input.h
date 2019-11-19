// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_OPTIONS_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_OPTIONS_LINE_INPUT_H_

#include "lib/fit/result.h"
#include "src/lib/line_input/line_input.h"

namespace line_input {

// And empty error message (on result.is_error()) means that the option querying was canceled.
using OptionsCallback = fit::callback<void(fit::result<void, std::string>,  // Result.
                                           std::vector<int>)>;              // Chosen options.

// Class that handles the options parsing and dispatching. This class is not meant to be used
// directly, but rather to be subclassed into classes that expose some interaction surface. See
// |OptionsLineInputStdout| below for an example.
//
// The basic concept is that it stores the given options and then receives a line string which
// then parses.
//
// TODO: Add an option for "all".
// TODO: Add an option for "none".
class OptionsLineInputBase {
 public:
  void PromptOptions(const std::vector<std::string>& options, OptionsCallback callback);

  bool is_active() const { return !!callback_; }

 protected:
  // Parsing is as follows: Pass in space separated indices (one-based: eg. 1 2 13). Then this will
  // be verified and returned in the callback as either the vector of indices (zero-based) or an
  // error. The given |callback| will always be called upon handling the line (on |HandleLine|),
  // whether there was an error or not.
  void HandleLine(const std::string& line, bool canceled);

 private:
  std::vector<std::string> options_;
  OptionsCallback callback_;
};

// Command line version of getting an option.
// Example usage (simplified):
//
// std::vector<std::string> options = <SOME_OPTIONS>;
//
// // This will the lambda when the input line has been processed.
// // Similar to how vanilla LineInput works.
// options_line_input.PromptOptions(options, [options](fit::result<void, std::string> result,
//                                     std::vector<int> chosen_options) {
//
//
//   if (result.is_error()) {
//      if (result.error().empty()) {
//        // Input got canceled (eg. ctrl-d).
//        return;
//      }
//
//      // result.error() has the error message (eg. "Invalid index <index_input>").
//      return;
//   }
//
//   for (int i : chosen_options) {
//      auto& option = options[i];
//
//      // <DO SOMETHING WITH OPTION>
//   }
// });
class OptionsLineInputStdout : public OptionsLineInputBase, public LineInputStdout {
 public:
  OptionsLineInputStdout(const std::string& prompt);
};

}  // namespace line_input

#endif  // SRC_LIB_LINE_INPUT_OPTIONS_LINE_INPUT_H_
