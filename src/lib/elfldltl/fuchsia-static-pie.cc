// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia-static-pie.h"

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/relro.h>
#include <lib/elfldltl/static-pie-with-vdso.h>
#include <zircon/syscalls.h>

void StaticPieSetup(const void* vdso_base) {
  // No meaningful diagnostics are possible before vDSO linking is done so
  // system calls can be made to write messages of any kind.  Just crash fast.
  auto diag = elfldltl::TrapDiagnostics();
  elfldltl::LinkStaticPieWithVdso(elfldltl::Self<>(), diag, vdso_base);
}

zx_status_t StaticPieRelro(zx_handle_t loaded_vmar) {
  const auto phdrs = elfldltl::Self<>::Phdrs();
  const size_t pagesize = _zx_system_get_page_size();
  if (auto [start, size] = elfldltl::RelroBounds(phdrs, pagesize); size > 0) {
    start += elfldltl::Self<>::LoadBias();
    return _zx_vmar_protect(loaded_vmar, ZX_VM_PERM_READ, start, size);
  }
  return ZX_OK;
}
