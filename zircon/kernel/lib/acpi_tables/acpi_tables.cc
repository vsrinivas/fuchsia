// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/acpi_tables.h"

#include <assert.h>
#include <err.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/apic.h>
#include <lib/acpi_lite/structures.h>
#include <trace.h>

#include <ktl/optional.h>
#include <ktl/span.h>
#include <lk/init.h>

#define LOCAL_TRACE 0

void AcpiTables::SetDefault(const AcpiTables* table) { default_ = table; }

const AcpiTables& AcpiTables::Default() {
  ASSERT_MSG(default_ != nullptr, "AcpiTables::SetDefault() must be called.");
  return *default_;
}
