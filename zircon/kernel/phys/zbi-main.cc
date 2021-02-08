// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/uart/all.h>
#include <lib/zbitl/view.h>
#include <stdio.h>

#include <limits>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "main.h"

FILE FILE::stdout_;

void PhysMain(void* zbi, arch::EarlyTicks ticks) {
  // Apply any relocations required to ourself.
  ApplyRelocations();

  // The serial console starts out as the uart::null driver that just drops
  // everything on the floor.  This is local in PhysMain rather than being
  // global so it can be nontrivally default-constructed in case that's needed.
  // The global stdout points into it, which would usually be a red flag with a
  // local variable, but that's OK here since this function can never return.
  uart::all::KernelDriver<uart::BasicIoProvider, uart::Unsynchronized> uart;

  // This must be called after uart is reset to make stdout use the new value.
  auto set_stdout = [&uart]() {
    uart.Visit([](auto&& driver) {
      // Initialize the selected serial console driver so driver.Write() works.
      driver.Init();

      // Point stdout at it so printf calls driver.Write().
      FILE::stdout_ = FILE{&driver};
    });
  };

  // Initialize stdout early to use the "null" (bit bucket) driver, so
  // any random printf calls from the library code don't crash.
  set_stdout();

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

  // Initialize kernel.serial from whatever we chose based on ZBI items.
  static BootOptions boot_opts;
  boot_opts.serial = uart.uart();

  // Now process command line items from the ZBI to set boot options.  This is
  // a separate loop so that kernel.serial settings override any ZBI item that
  // chose a UART, regardless of the relative order of UART and CMDLINE items.
  // The last word in the last CMDLINE item always wins.
  for (auto [header, payload] : zbi_view) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      boot_opts.SetMany({reinterpret_cast<const char*>(payload.data()), payload.size()});
    }
  }
  zbi_view.ignore_error();

  // Now copy the configuration possibly changed by kernel.serial back in.
  uart = boot_opts.serial;

  // Reinitialize stdout to use what the ZBI or command line requested.  Note
  // we don't do this after parsing ZBI items and before parsing command line
  // options, because if kernel.serial overrode what the ZBI items said, we
  // shouldn't be sending output to the wrong UART in between.
  set_stdout();

  // The global is a pointer just for uniformity between the code in phys and
  // in the kernel proper.
  gBootOptions = &boot_opts;

  // Call the real entry point now that it can use printf!  It does not return.
  ZbiMain(zbi, ticks);
}
