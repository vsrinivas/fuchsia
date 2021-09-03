// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <stdarg.h>
#include <stdio.h>

#include <phys/stdio.h>

FILE FILE::stdout_;

UartDriver& GetUartDriver() {
  static UartDriver uart;
  return uart;
}

void ConfigureStdout(const uart::all::Driver& uart) {
  GetUartDriver() = uart;
  GetUartDriver().Visit([](auto&& driver) {
    driver.Init();

    // Update stdout global to write to the configured driver.
    FILE::stdout_ = FILE{&driver};
  });
}

void debugf(const char* fmt, ...) {
  if (gBootOptions && !gBootOptions->phys_verbose) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
