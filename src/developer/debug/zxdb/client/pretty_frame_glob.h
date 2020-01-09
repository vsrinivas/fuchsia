// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_FRAME_GLOB_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_FRAME_GLOB_H_

#include <optional>
#include <string>

namespace zxdb {

class Frame;
class Location;

// A type of glob for matching stack frames. It can match a file or function name.
class PrettyFrameGlob {
 public:
  // This class uses static constructors due to the ambiguity of representing file and function
  // matchers.

  // Matches any stack frame. The min/max_matches allows this to match some number of frames
  // (inclusive).
  static PrettyFrameGlob Wildcard(size_t min_matches = 1, size_t max_matches = 1);

  static PrettyFrameGlob File(std::string file);
  static PrettyFrameGlob Func(std::string function);
  static PrettyFrameGlob FuncFile(std::string function, std::string file);

  bool is_wildcard() const { return !function_ && !file_; }

  size_t min_matches() const { return min_matches_; }
  size_t max_matches() const { return max_matches_; }

  bool Matches(const Frame* frame) const;
  bool Matches(const Location& loc) const;

 private:
  size_t min_matches_ = 1;
  size_t max_matches_ = 1;

  std::optional<std::string> function_;
  std::optional<std::string> file_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_FRAME_GLOB_H_
