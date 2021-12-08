// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/static-pie.h>

#include <phys/main.h>

void ApplyRelocations() {
  // There is never anything to do when compiled as fixed-position, which is
  // used only for x86-32.  When compiled as PIC, the phys program may still be
  // linked as fixed-position, observed at runtime as Self::LoadBias() == 0.
#ifdef __PIC__
  auto diag = elfldltl::TrapDiagnostics();
  elfldltl::LinkStaticPie(elfldltl::Self<>(), diag, PHYS_LOAD_ADDRESS, _end);
#endif
}
