// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parse_result.h"

#include <deque>

namespace shell::parser {

const ParseResult ParseResult::kEnd("", 0, 0, 0, 0);

}  // namespace shell::parser
