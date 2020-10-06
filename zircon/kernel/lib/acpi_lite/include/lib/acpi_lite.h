// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_H_

#include <lib/acpi_lite/structures.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/function.h>

namespace acpi_lite {

class PhysMemReader;

// Functionality for reading ACPI tables.
class AcpiParser {
 public:
  AcpiParser(const AcpiParser&) = default;
  AcpiParser& operator=(const AcpiParser&) = default;

  // Create a new AcpiParser, starting at the given Root System Description Pointer (RSDP).
  static zx::status<AcpiParser> Init(zx_paddr_t rsdp_pa);

  // Create a new AcpiParser, using the given PhysMemReader object.
  //
  // PhysMemReader must outlive this object. Caller retains ownership of the PhysMemReader.
  static zx::status<AcpiParser> Init(PhysMemReader& physmem_reader, zx_paddr_t rsdp_pa);

  // Get the number of tables.
  inline size_t num_tables() const { return num_tables_; }

  // Get the first table matching the given signature. Return nullptr if no table found.
  const acpi_sdt_header* GetTableBySignature(const char* sig) const;

  // Return the i'th table. Return nullptr if the index is out of range.
  const acpi_sdt_header* GetTableAtIndex(size_t index) const;

  // Print tables to debug output.
  void DumpTables() const;

  // Iterate over the Multiple APIC Description Table (MADT) entries,
  // calling the given callback once per entry.
  using MadtEntryCallback = fbl::Function<void(const void* entry, size_t entry_len)>;
  zx_status_t EnumerateMadtEntries(uint8_t search_type, const MadtEntryCallback&) const;

 private:
  // Create a new AcpiParser.
  //
  // |reader| and |sdt| must outlive the created instance.
  AcpiParser(PhysMemReader& reader, const acpi_rsdt_xsdt& sdt, size_t num_tables, bool xsdt)
      : reader_(&reader), sdt_(&sdt), num_tables_(num_tables), xsdt_(xsdt) {}

  PhysMemReader* reader_;      // Owned elsewhere. Non-null.
  const acpi_rsdt_xsdt* sdt_;  // Owned elsewhere. Non-null.
  size_t num_tables_;          // Number of top level tables
  bool xsdt_;                  // If true, we are using the extended format and pointers are 64 bit.
};

// A PhysMemReader translates physical addresses (such as those in the ACPI tables and the RSDT
// itself) into pointers directly readable by the acpi_lite library.
class PhysMemReader {
 public:
  virtual ~PhysMemReader() = default;
  virtual zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) = 0;
};

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_H_
