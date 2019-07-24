// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_

#include <utility>
#include <vector>

#include "lib/fit/defer.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

class FormatNode;
struct FormatOptions;
class PrettyType;
class Type;

class PrettyTypeManager {
 public:
  PrettyTypeManager();
  ~PrettyTypeManager();

  // Finds a PrettyType associated with the given type object. Returns a non-owning pointer if
  // found. Returns null if there is nothing registered for this type.
  //
  // The type can be null which will report no PrettyType.
  PrettyType* GetForType(const Type* type) const;

  // Attempts to format the given node with a pretty printer. If there is a pretty-printer it will
  // take ownership of the callback (and maybe issue it immediately if the formatting was
  // synchronous) and return true.
  //
  // The concrete type is passed in.
  //
  // If there is no pretty type registered, does nothing with the callback and returns false.
  bool Format(FormatNode* node, const Type* type, const FormatOptions& options,
              fxl::RefPtr<EvalContext> context, fit::deferred_callback& cb) const;

 private:
  void AddDefaultCppPrettyTypes();
  void AddDefaultRustPrettyTypes();

  // These map prefixes of full type names to a pretty-printer for that prefix. In the future we
  // may need more general glob support. It would also be nice to have an optimized prefix match
  // to avoid the brute-force search (even if we support globs) since that will be the common case.
  //
  // This will pick the longest match.
  using PrefixPrettyType = std::pair<std::string, std::unique_ptr<PrettyType>>;
  std::vector<PrefixPrettyType> cpp_;
  std::vector<PrefixPrettyType> rust_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrettyTypeManager);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_
