// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_LANGUAGE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_LANGUAGE_H_

#include "src/developer/debug/zxdb/symbols/dwarf_lang.h"

namespace zxdb {

// The enum values should be bits so we can store the language applicability for each token in a
// bitfield.
enum class ExprLanguage {
  // All C, C++, and Objective C variants. We may need to split out objective C in the future.
  kC = 1,

  kRust = 2
};

// All non-Rust languages are treated as C.
inline ExprLanguage DwarfLangToExprLanguage(DwarfLang dwarf) {
  if (dwarf == DwarfLang::kRust)
    return ExprLanguage::kRust;
  return ExprLanguage::kC;
}

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_LANGUAGE_H_
