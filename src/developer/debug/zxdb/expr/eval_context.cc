// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_context.h"

#include "src/developer/debug/zxdb/expr/found_name.h"

namespace zxdb {

FoundName EvalContext::FindName(const FindNameOptions& options,
                                const ParsedIdentifier& identifier) const {
  FindNameOptions new_opts(options);
  new_opts.max_results = 1;

  std::vector<FoundName> results;
  FindName(new_opts, identifier, &results);
  if (!results.empty())
    return std::move(results[0]);
  return FoundName();
}

}  // namespace zxdb
