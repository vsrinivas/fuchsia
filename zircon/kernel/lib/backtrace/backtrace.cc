// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/backtrace.h"

#include <lib/version.h>

static constexpr const char* kFormat = "{{{bt:%zu:%p:%s}}}\n";
static constexpr const char* kRa = "ra";
static constexpr const char* kPc = "pc";

void Backtrace::Print(FILE* file) const {
  print_backtrace_version_info(file);
  PrintWithoutVersion(file);
}

void Backtrace::PrintWithoutVersion(FILE* file) const {
  for (size_t i = 0; i < size_; ++i) {
    const char* type = kRa;
    if ((first_frame_type_ == FrameType::PreciseLocation) && (i == 0)) {
      type = kPc;
    }
    fprintf(file, kFormat, i, reinterpret_cast<void*>(addr_[i]), type);
  }
}
