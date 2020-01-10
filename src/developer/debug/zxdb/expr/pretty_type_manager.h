// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_

#include <map>
#include <utility>
#include <vector>

#include "lib/fit/defer.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/identifier_glob.h"
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

  // Adds a PrettyType for the givene language/glob.
  void Add(ExprLanguage lang, IdentifierGlob glob, std::unique_ptr<PrettyType> pretty);

  // Finds a PrettyType associated with the given type object. Returns a non-owning pointer if
  // found. Returns null if there is nothing registered for this type.
  //
  // The type can be null which will report no PrettyType.
  PrettyType* GetForType(const Type* type) const;

  // Attempts to format the given node with a pretty printer. If there is a pretty-printer it will
  // take ownership of the callback (and maybe issue it immediately if the formatting was
  // synchronous) and return true.
  //
  // The type will not be made concrete for the comparison because some pretty-printers match
  // typedef names which will get stripped. Therefore, callers may want to call twice, once with a
  // non-concrete type and once with the concrete one. This function will strip C-V qualifiers
  // for name comparison since those are always safe to strip.
  //
  // If there is no pretty type registered, does nothing with the callback and returns false.
  bool Format(FormatNode* node, const Type* type, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback& cb) const;

 private:
  void AddDefaultCppPrettyTypes();
  void AddDefaultRustPrettyTypes();

  // Fuchsia-specific libraries.
  void AddDefaultFuchsiaCppPrettyTypes();

  // These map globs of full type names to a pretty-printer for that prefix. In the future it might
  // be nice to have some kind of trie structure that would allow faster prefix lookup for the
  // non-varying parts of the globs.
  using PrefixPrettyType = std::pair<IdentifierGlob, std::unique_ptr<PrettyType>>;
  std::vector<PrefixPrettyType> cpp_;
  std::vector<PrefixPrettyType> rust_;

  // Pretty printer for all Rust tuple types
  std::unique_ptr<PrettyType> rust_tuple_type_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrettyTypeManager);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_MANAGER_H_
