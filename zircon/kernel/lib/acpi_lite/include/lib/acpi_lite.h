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

  // Create a new AcpiParser, using the given PhysMemReader object.
  //
  // PhysMemReader must outlive this object. Caller retains ownership of the PhysMemReader.
  static zx::status<AcpiParser> Init(PhysMemReader& physmem_reader, zx_paddr_t rsdp_pa);

  // Get the number of tables.
  inline size_t num_tables() const { return num_tables_; }

  // Get the first table matching the given signature. Return nullptr if no table found.
  const AcpiSdtHeader* GetTableBySignature(AcpiSignature sig) const;

  // Return the i'th table. Return nullptr if the index is out of range.
  const AcpiSdtHeader* GetTableAtIndex(size_t index) const;

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
  AcpiParser(PhysMemReader& reader, const AcpiRsdt* rsdt, const AcpiXsdt* xsdt, size_t num_tables)
      : reader_(&reader), rsdt_(rsdt), xsdt_(xsdt), num_tables_(num_tables) {}

  PhysMemReader* reader_;  // Owned elsewhere. Non-null.
  const AcpiRsdt* rsdt_;   // Owned elsewhere. May be null.
  const AcpiXsdt* xsdt_;   // Owned elsewhere. May be null.
  size_t num_tables_;      // Number of top level tables
};

// A PhysMemReader translates physical addresses (such as those in the ACPI tables and the RSDT
// itself) into pointers directly readable by the acpi_lite library.
class PhysMemReader {
 public:
  virtual ~PhysMemReader() = default;
  virtual zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) = 0;
};

//
// Functions below exposed for testing.
//

// Ensure the checksum of the given block of code is valid.
bool AcpiChecksumValid(const void* buf, size_t len);

// Calculate a checksum of the given range of memory.
uint8_t AcpiChecksum(const void* _buf, size_t len);

// Validate the RSDT / XSDT tables.
zx::status<const AcpiRsdt*> ValidateRsdt(PhysMemReader& reader, uint32_t rsdt_pa,
                                         size_t* num_tables);
zx::status<const AcpiXsdt*> ValidateXsdt(PhysMemReader& reader, uint32_t rsdt_pa,
                                         size_t* num_tables);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_H_
