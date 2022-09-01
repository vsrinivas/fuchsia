// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/file_line_function.h"

#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug {

bool operator<(const FileLineFunction& a, const FileLineFunction& b) {
  if (a.line() != b.line())
    return a.line() < b.line();
  return a.file() < b.file();
}

std::string FileLineFunction::ToString() const {
  if (!is_valid()) {
    return "";
  }
  if (!function_) {
    return fxl::StringPrintf("[%s:%d]", file_, line_);
  }
  return fxl::StringPrintf("[%s:%d][%s]", file_, line_, function_);
}

bool operator==(const FileLineFunction& a, const FileLineFunction& b) {
  return a.line() == b.line() && a.file() == b.file();
}

bool operator!=(const FileLineFunction& a, const FileLineFunction& b) { return !operator==(a, b); }

}  // namespace debug
