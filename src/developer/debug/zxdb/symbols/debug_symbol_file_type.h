// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DEBUG_SYMBOL_FILE_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DEBUG_SYMBOL_FILE_TYPE_H_

namespace zxdb {

enum class DebugSymbolFileType {
  kDebugInfo,
  kBinary,
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DEBUG_SYMBOL_FILE_TYPE_H_
