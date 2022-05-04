// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <re2/re2.h>

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
  // |replacements| is a mapping from regular expressions to replacements.
  // Best illustration is an example. This example removes koids, timestamps
  // ("ts"), and lowercase hex numbers:
  //
  // Create({
  //   { "koid\\([0-9]+\\)", "koid(<>)" },
  //   { "ts: [0-9]+", "ts: <>" },
  //   { "0x[0-9a-f]+", "<>" },
  // });
  //
  // So "ts: 123 mumble koid(456) foo, bar 0xabcd"
  // becomes "ts: <> mumble koid(<>) foo, bar <>".
  static std::unique_ptr<Squelcher> Create(
      const std::vector<std::pair<std::string_view, std::string_view>>& replacements);

  fbl::String Squelch(const char* str);

 private:
  explicit Squelcher(
      const std::vector<std::pair<std::string_view, std::string_view>>& replacements);

  // Need to use a unique_ptr because RE2 is not movable or copyable.
  std::vector<std::pair<std::unique_ptr<re2::RE2>, std::string>> compiled_replacements_;
};

}  // namespace trace_testing
