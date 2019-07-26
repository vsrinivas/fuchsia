// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <regex.h>

#include <memory>

// TODO(dje): Conversion to std::string needs to be done in conjunction with
// converting trace-reader.
#include <fbl/string.h>

namespace trace_testing {

// Squelcher is used to filter out elements of a trace record that may
// vary run to run or even within a run and are not germaine to determining
// correctness. The canonical example is record timestamps.
// The term "squelch" derives from radio circuitry used to remove noise.

class Squelcher {
 public:
  // |regex_str| is a regular expression consistenting of one or more
  // subexpressions, the text in the parenthesis of each matching expressions
  // is replaced with '<>'.
  // Best illustration is an example. This example removes decimal numbers,
  // koids, timestamps ("ts"), and lowercase hex numbers.
  // const char regex[] = "([0-9]+/[0-9]+)"
  //   "|koid\\(([0-9]+)\\)"
  //   "|koid: ([0-9]+)"
  //   "|ts: ([0-9]+)"
  //   "|(0x[0-9a-f]+)";
  // So "ts: 123 42 mumble koid(456) foo koid: 789, bar 0xabcd"
  // becomes "ts: <> <> mumble koid(<>) foo koid: <>, bar <>".
  static std::unique_ptr<Squelcher> Create(const char* regex_str);

  ~Squelcher();

  fbl::String Squelch(const char* str);

 private:
  Squelcher() = default;

  // The compiled regex.
  regex_t regex_;
};

}  // namespace trace_testing
