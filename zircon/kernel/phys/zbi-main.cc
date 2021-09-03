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
#include <phys/main.h>
#include <phys/stdio.h>

void PhysMain(void* zbi, arch::EarlyTicks ticks) {
  // Apply any relocations required to ourself.
  ApplyRelocations();

  // Initially set up stdout to write to the null uart driver.
  ConfigureStdout();

  // Scan through the ZBI looking for items that configure the serial console.
  // Note that as each item is encountered, it resets uart to the appropriate
  // variant and sets its configuration values.  So a later item will override
  // the selection and configuration of an earlier item.  But this all happens
  // before anything touches hardware.
  UartDriver& uart = GetUartDriver();
  zbitl::View<zbitl::ByteView> zbi_view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};
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
  boot_opts.serial_source = OptionSource::kZbi;

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

  // Configure the selected UART.
  //
  // Note we don't do this after parsing ZBI items and before parsing command
  // line options, because if kernel.serial overrode what the ZBI items said,
  // we shouldn't be sending output to the wrong UART in between.
  ConfigureStdout(boot_opts.serial);

  // The global is a pointer just for uniformity between the code in phys and
  // in the kernel proper.
  gBootOptions = &boot_opts;

  // Perform any architecture-specific set up.
  ArchSetUp();

  // Call the real entry point now that it can use printf!  It does not return.
  ZbiMain(zbi, ticks);
}
