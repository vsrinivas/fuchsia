// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <stdlib.h>

#include <ktl/span.h>
#include <phys/efi/main.h>

#include "test-main.h"

#include <ktl/enforce.h>

PHYS_SINGLETHREAD int main(int argc, char** argv) {
  // Early boot may have filled the screen with logs. Add a newline to
  // terminate any previous line, and another newline to leave a blank.
  printf("\n\n");

  if (argc > 0) {
    printf("*** UEFI test application arguments ***");
    for (const char* arg : ktl::span(argv, argc)) {
      printf(" \"%s\"", arg);
    }
    printf("\n");
  }

  // Run the test.
  int status = TestMain(nullptr, gEfiEntryTicks);
  if (status == 0) {
    printf("\n*** Test succeeded ***\n%s\n\n", ZBI_TEST_SUCCESS_STRING);
  } else {
    printf("\n*** Test FAILED: status %d ***\n\n", status);
  }

  return status;
}
