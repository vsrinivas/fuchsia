// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOL_INDEX_ERROR_H_
#define TOOLS_SYMBOL_INDEX_ERROR_H_

#include <string>

namespace symbol_index {

// For now, let's just use a string to represent an error.
// An empty string means no error.
using Error = std::string;

}  // namespace symbol_index

#endif  // TOOLS_SYMBOL_INDEX_ERROR_H_
