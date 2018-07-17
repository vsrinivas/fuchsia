// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/code_block.h"

namespace zxdb {

CodeBlock::CodeBlock(int tag) : Symbol(tag) {}
CodeBlock::~CodeBlock() = default;

}  // namespace zxdb
