// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/qemu.h>
#include <stdio.h>
#include <stdlib.h>

#include <phys/main.h>
#include <phys/stdio.h>
#include <phys/uart.h>

#include "test-main.h"

void PhysMain(void* bootloader_data, arch::EarlyTicks ticks) {
  // Apply any relocations required to ourself.
  ApplyRelocations();

  InitStdout();

  static uart::qemu::KernelDriver<> uart;
  SetUartConsole(uart.uart());

  // Early boot may have filled the screen with logs. Add a newline to
  // terminate any previous line, and another newline to leave a blank.
  printf("\n\n");

  // Run the test.
  int status = TestMain(bootloader_data, ticks);
  if (status == 0) {
    printf("\n*** Test succeeded ***\n%s\n\n", BOOT_TEST_SUCCESS_STRING);
  } else {
    printf("\n*** Test FAILED: status %d ***\n\n", status);
  }

  // No way to shut down.
  abort();
}
