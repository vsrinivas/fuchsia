// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/static-pie.h>

#include <phys/main.h>

void ApplyRelocations() {
#ifdef ZX_STATIC_PIE
  auto diag = elfldltl::TrapDiagnostics();
  elfldltl::LinkStaticPie(elfldltl::Self<>(), diag, PHYS_LOAD_ADDRESS, _end);
#endif
}
