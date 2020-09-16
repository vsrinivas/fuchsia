// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_ERROR_H_
#define TOOLS_SYMBOLIZER_ERROR_H_

#include <string>

namespace symbolizer {

// For now, let's just use a string to represent an error.
// An empty string means no error.
using Error = std::string;

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_ERROR_H_
