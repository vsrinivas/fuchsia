// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ktl/iterator.h>
#include <phys/symbolize.h>

#include "turducken.h"

const char Symbolize::kProgramName_[] = "chain-load-test";

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  Load(kernel_item, ktl::next(kernel_item), boot_zbi().end());
  Boot();
  /*NOTREACHED*/
}
