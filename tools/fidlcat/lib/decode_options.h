// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
#define TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_

struct DecodeOptions {
  // If a syscall satisfies one of these filters, it can be displayed.
  std::vector<std::string> syscall_filters;
  // But it is only displayed if it doesn't satifies any of these filters.
  std::vector<std::string> exclude_syscall_filters;
};

#endif  // TOOLS_FIDLCAT_LIB_DECODE_OPTIONS_H_
