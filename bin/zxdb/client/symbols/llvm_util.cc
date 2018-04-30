// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/llvm_util.h"

#include "llvm/DebugInfo/DIContext.h"

namespace zxdb {

Symbol SymbolFromDILineInfo(const llvm::DILineInfo& info) {
  if (!info)
    return Symbol();

  return Symbol(info.FileName, info.FunctionName, info.Line, info.Column,
                info.StartLine);
}

}  // namespace zxdb
