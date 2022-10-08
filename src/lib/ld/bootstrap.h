// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/static-pie-with-vdso.h>

namespace ld {

void StaticPieSetup(const void* vdso_base) {
  // No meaningful diagnostics are possible before vDSO linking is done so
  // system calls can be made to write messages of any kind.  Just crash fast.
  auto diag = elfldltl::TrapDiagnostics();
  elfldltl::LinkStaticPieWithVdso(elfldltl::Self<>(), diag, vdso_base);
}

}  // namespace ld
