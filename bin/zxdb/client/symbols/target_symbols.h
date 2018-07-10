// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class FileLine;

// Symbol interface for a Target. A target may or may not have a process, so
// this interface does not deal with anything related to addresses. See
// ProcessSymbols for that (which is most of the stuff).
//
// We can know about symbols associated with a target even when the process
// isn't loaded. For example, when setting a breakpoint on a symbol we can
// validate that it's a real symbol.
class TargetSymbols {
 public:
  TargetSymbols();
  virtual ~TargetSymbols();

  // Gets file matches across all known modules. See
  // ModuleSymbols::FindFileMatches().
  virtual std::vector<std::string> FindFileMatches(
      const std::string& name) const = 0;

  // Returns the source location location(s) for the given symbol.
  virtual std::vector<FileLine> FindLinesForSymbol(
      const std::string& name) const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TargetSymbols);
};

}  // namespace zxdb
