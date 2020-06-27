// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <stdlib.h>

#include "../main.h"
#include "test-main.h"

void ZbiMain(void* zbi, arch::EarlyTicks ticks) {
  int status = TestMain(zbi, ticks);
  if (status == 0) {
    printf("*** Test succeeded ***\n%s\n", ZBI_TEST_SUCCESS_STRING);
  } else {
    printf("*** Test FAILED: status %d ***\n", status);
  }

  // No way to shut down.
  abort();
}
