// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/all.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <limits>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "main.h"

FILE FILE::stdout_;

void PhysMain(void* zbi, arch::EarlyTicks ticks) {
  // The serial console starts out as the uart::null driver that just drops
  // everything on the floor.
  uart::all::KernelDriver<uart::BasicIoProvider, uart::Unsynchronized> uart;

  // Scan through the ZBI looking for items that configure the serial console.
  // Note that as each item is encountered, it resets uart to the appropriate
  // variant and sets its configuration values.  So a later item will override
  // the selection and configuration of an earlier item.  But this all happens
  // before anything touches hardware.
  zbitl::PermissiveView<ktl::span<ktl::byte>> zbi_view(
      // We don't have any outside information on the maximum size of the
      // ZBI.  We'll just have to trust the length in the ZBI header, so tell
      // zbitl that the memory storing it is as large as a ZBI could ever be.
      {reinterpret_cast<ktl::byte*>(zbi), UINT32_MAX});
  for (auto [header, payload] : zbi_view) {
    uart.Match(*header, payload.data());
  }
  // Don't bother with any errors reading the ZBI.  Either the console got set
  // up or it didn't.  If the program cares about the ZBI being valid, it will
  // scan it again.
  zbi_view.ignore_error();

  uart.Visit([](auto&& driver) {
    // Initialize the selected serial console driver so driver.Write() works.
    driver.Init();

    // Point stdout at it so printf calls driver.Write().
    FILE::stdout_ = FILE{&driver};
  });

  // Call the real entry point now that it can use printf!  It does not return.
  ZbiMain(zbi, ticks);
}
