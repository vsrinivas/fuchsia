// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef KERNEL_LIB_ACPI_TABLES_H
#define KERNEL_LIB_ACPI_TABLES_H

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <zircon/types.h>

#include <arch/x86/apic.h>
#include <fbl/function.h>

// Designed to read and parse APIC tables, other functions of the APIC
// subsystem are out of scope of this class. This class can work before dynamic memory
// allocation is available.
class AcpiTables {
 public:
  explicit AcpiTables(const acpi_lite::AcpiParserInterface* tables) {}

  // Set / get a default instance of AcpiTables.
  //
  // Caller is responsible for synchronising SetDefault(), typically by
  // calling this during system startup.
  //
  // These default instances allow existing production code to use an
  // AcpiTable instance without having to plumb it everywhere.
  static void SetDefault(const AcpiTables* table);
  static const AcpiTables& Default();

  // Initialise a default instance using the acpi_lite library.

 private:
  inline static const AcpiTables* default_ = nullptr;
};

#endif  // KERNEL_LIB_APIC_TABLES_H
