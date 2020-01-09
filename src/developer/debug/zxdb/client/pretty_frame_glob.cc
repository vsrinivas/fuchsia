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
PrettyFrameGlob PrettyFrameGlob::Func(IdentifierGlob glob) {
  PrettyFrameGlob result;
  result.function_ = std::move(glob);
  return result;
}

// static
PrettyFrameGlob PrettyFrameGlob::FuncFile(IdentifierGlob func_glob, std::string file) {
  PrettyFrameGlob result;
  result.function_ = std::move(func_glob);
  result.file_ = std::move(file);
  return result;
}

// static
PrettyFrameGlob PrettyFrameGlob::Func(const std::string& func_glob) {
  IdentifierGlob glob;
  Err err = glob.Init(func_glob);
  FXL_DCHECK(err.ok());

  return Func(std::move(glob));
}

// static
PrettyFrameGlob PrettyFrameGlob::FuncFile(const std::string& func_glob, std::string file) {
  IdentifierGlob glob;
  Err err = glob.Init(func_glob);
  FXL_DCHECK(err.ok());

  return FuncFile(std::move(glob), std::move(file));
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
    const Symbol* symbol = loc.symbol().Get();
    if (!symbol)
      return false;  // No function to match.

    if (!function_->Matches(ToParsedIdentifier(symbol->GetIdentifier())))
      return false;
  }

  return true;
}

}  // namespace zxdb
