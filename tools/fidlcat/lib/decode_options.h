// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
#define TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_

#include <regex>

struct DecodeOptions {
  // If a syscall satisfies one of these filters, it can be displayed.
  std::vector<std::regex> syscall_filters;
  // But it is only displayed if it doesn't satisfy any of these filters.
  std::vector<std::regex> exclude_syscall_filters;
};

#endif  // TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
