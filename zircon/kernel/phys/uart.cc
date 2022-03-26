// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <phys/stdio.h>
#include <phys/uart.h>

UartDriver& GetUartDriver() {
  static UartDriver uart;
  return uart;
}

void SetUartConsole(const uart::all::Driver& uart) {
  GetUartDriver() = uart;
  GetUartDriver().Visit([](auto&& driver) {
    driver.Init();

    // Update stdout global to write to the configured driver.
    PhysConsole::Get().set_serial(FILE{&driver});
  });
}
