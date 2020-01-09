// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/pretty_frame_glob.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

// static
PrettyFrameGlob PrettyFrameGlob::Wildcard(size_t min_matches, size_t max_matches) {
  PrettyFrameGlob result;
  result.min_matches_ = min_matches;
  result.max_matches_ = max_matches;
  return result;
}

// static
PrettyFrameGlob PrettyFrameGlob::File(std::string file) {
  PrettyFrameGlob result;
  result.file_ = std::move(file);
  return result;
}

// static
PrettyFrameGlob PrettyFrameGlob::Func(std::string function) {
  PrettyFrameGlob result;
  result.function_ = std::move(function);
  return result;
}

bool PrettyFrameGlob::Matches(const Frame* frame) const {
  if (is_wildcard())
    return true;  // Matches everything. Avoid symbolizing in GetLocation if unnecessary.
  return Matches(frame->GetLocation());
}

bool PrettyFrameGlob::Matches(const Location& loc) const {
  if (is_wildcard())
    return true;  // Matches everything.
  if (!loc.has_symbols())
    return false;  // Can't match something with no symbols.

  if (file_) {
    if (!PathContainsFromRight(loc.file_line().file(), *file_))
      return false;
  }

  if (function_) {
    const Function* function = loc.symbol().Get()->AsFunction();
    if (!function)
      return false;  // No function to match.

    // Currently we require an exact function name match.
    if (function->GetFullName() != *function_)
      return false;
  }

  return true;
}

}  // namespace zxdb
