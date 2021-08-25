// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_FORMATTER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_FORMATTER_H_

#include <utility>

#include "experimental_flags.h"
#include "raw_ast.h"
#include "reporter.h"
#include "span_sequence.h"
#include "span_sequence_tree_visitor.h"

namespace fidl::fmt {

class NewFormatter final {
 public:
  explicit NewFormatter(size_t cols, reporter::Reporter* reporter)
      : cols_(cols), reporter_(reporter){};

  std::optional<std::string> Format(const fidl::SourceFile& source_file,
                                    const fidl::ExperimentalFlags& experimental_flags) const;

 private:
  std::string Print(std::unique_ptr<raw::File> ast, size_t original_file_size) const;

  const size_t cols_;
  reporter::Reporter* reporter_;
};

}  // namespace fidl::fmt

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_FORMATTER_H_
