// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/acpi.h"

#include <inttypes.h>
#include <zircon/errors.h>

#include <fbl/no_destructor.h>
#include <phys/symbolize.h>

namespace {

class PhysMemReader final : public acpi_lite::PhysMemReader {
 public:
  constexpr PhysMemReader() = default;

  zx::result<const void*> PhysToPtr(uintptr_t phys, size_t length) final {
    return zx::success(reinterpret_cast<const void*>(phys));
  }
};

}  // namespace

zx::result<acpi_lite::AcpiParser> MakeAcpiParser(uint64_t acpi_rsdp) {
  static fbl::NoDestructor<PhysMemReader> reader;
  if (static_cast<uintptr_t>(acpi_rsdp) != acpi_rsdp) {
    printf("%s: ACPI tables found at (%#" PRIx64 ") not within lower 4GiB\n", ProgramName(),
           acpi_rsdp);
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  return acpi_lite::AcpiParser::Init(*reader, static_cast<zx_paddr_t>(acpi_rsdp));
}
