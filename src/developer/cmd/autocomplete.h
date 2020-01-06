// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CMD_AUTOCOMPLETE_H_
#define SRC_DEVELOPER_CMD_AUTOCOMPLETE_H_

#include <lib/fit/function.h>

#include <string>
#include <vector>

namespace cmd {

class Autocomplete {
 public:
  // Create an |Autocomplete| object for the given |line|.
  explicit Autocomplete(const std::string& line);
  ~Autocomplete();

  Autocomplete(const Autocomplete&) = delete;
  Autocomplete& operator=(const Autocomplete&) = delete;
  Autocomplete(Autocomplete&&) = delete;
  Autocomplete& operator=(Autocomplete&&) = delete;

  // List of compete tokens extracted from |line|.
  //
  // Does not include the |fragment|, which the sequence of non-whitespace
  // characters that the user is currently typing.
  //
  // Tokenization for autocomplete does not take comments or quoting into
  // consideration, which means the tokenization performed by |Command| can be
  // different than this tokenization.
  const std::vector<std::string> tokens() const { return tokens_; };

  // The part of the |line| that needs to be completed.
  //
  // The non-whitespace characters at the end of |line|.
  const std::string& fragment() const { return fragment_; };

  // Adds a possible completion of |fragment|.
  void AddCompletion(const std::string& completion);

  // Attempt to complete |fragment| as a path.
  //
  // If |fragment| appears to be a relative path, this method will attempt to
  // complete the path relative to the current working directory.
  void CompleteAsPath();

  // Attempt to complete |fragment| as an entry in the given |directory|.
  //
  // Will not find any completions if |fragment| contains a "/" character
  // because directory entries never contain "/" characters.
  void CompleteAsDirectoryEntry(const std::string& directory);

  // Attempt to complete |fragment| as an environment variable name.
  void CompleteAsEnvironmentVariable();

  // Return all the suggested completions for |line|.
  //
  // After this method returns, the list of completions stored in this object is
  // empty.
  std::vector<std::string> TakeCompletions();

 private:
  std::vector<std::string> tokens_;
  std::string fragment_prefix_;
  std::string fragment_;

  std::vector<std::string> completions_;
};

}  // namespace cmd

#endif  // SRC_DEVELOPER_CMD_AUTOCOMPLETE_H_
