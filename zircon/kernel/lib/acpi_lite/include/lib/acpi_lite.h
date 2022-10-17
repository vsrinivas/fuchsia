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

namespace acpi_lite {

class PhysMemReader;

// Abstract interface for reading ACPI tables.
class AcpiParserInterface {
 public:
  virtual ~AcpiParserInterface() = default;

  // Get the number of tables.
  virtual size_t num_tables() const = 0;

  // Return the i'th table. Return nullptr if the index is out of range.
  //
  // If the return value is non-null, it is guaranteed that the returned
  // pointer |p| points to memory at least |p->length| bytes long.
  virtual const AcpiSdtHeader* GetTableAtIndex(size_t index) const = 0;
};

// Functionality for reading ACPI tables.
class AcpiParser final : public AcpiParserInterface {
 public:
  AcpiParser(const AcpiParser&) = default;
  AcpiParser& operator=(const AcpiParser&) = default;

  // Create a new AcpiParser, using the given PhysMemReader object.
  //
  // PhysMemReader must outlive this object. Caller retains ownership of the PhysMemReader.
  static zx::result<AcpiParser> Init(PhysMemReader& physmem_reader, zx_paddr_t rsdp_pa);

  uint64_t rsdp_pa() const { return rsdp_addr_; }

  // Print tables to debug output.
  void DumpTables() const;

  // |AcpiParserInterface| implementation.
  inline size_t num_tables() const final { return num_tables_; }
  const AcpiSdtHeader* GetTableAtIndex(size_t index) const final;

 private:
  // Create a new AcpiParser.
  //
  // |reader| and |sdt| must outlive the created instance.
  AcpiParser(PhysMemReader& reader, zx_paddr_t rsdp_addr, const AcpiRsdt* rsdt,
             const AcpiXsdt* xsdt, size_t num_tables, zx_paddr_t root_table_addr)
      : reader_(&reader),
        rsdt_(rsdt),
        xsdt_(xsdt),
        num_tables_(num_tables),
        root_table_addr_(root_table_addr),
        rsdp_addr_(rsdp_addr) {}

  // Get the physical address of the given table, or return 0 if the table does not exist.
  zx_paddr_t GetTablePhysAddr(size_t index) const;

  PhysMemReader* reader_;       // Owned elsewhere. Non-null.
  const AcpiRsdt* rsdt_;        // Owned elsewhere. May be null.
  const AcpiXsdt* xsdt_;        // Owned elsewhere. May be null.
  size_t num_tables_;           // Number of top level tables
  zx_paddr_t root_table_addr_;  // Physical address of the root table.
  zx_paddr_t rsdp_addr_;        // Physical address of the RSDP.
};

// Get the first table matching the given signature. Return nullptr if no table found.
const AcpiSdtHeader* GetTableBySignature(const AcpiParserInterface& parser, AcpiSignature sig);

// Get the first table of the given type. Return nullptr if no table found, or the
// table is invalid.,
template <typename T>
const T* GetTableByType(const AcpiParserInterface& parser) {
  const AcpiSdtHeader* header = GetTableBySignature(parser, T::kSignature);
  if (header == nullptr) {
    return nullptr;
  }
  // TODO(fxbug.dev/89248): Change this check so that tables with optional entries can be found on
  // platforms that do not have them
  if (header->length < sizeof(T)) {
    return nullptr;
  }
  return reinterpret_cast<const T*>(header);
}

// A PhysMemReader translates physical addresses (such as those in the ACPI tables and the RSDT
// itself) into pointers directly readable by the acpi_lite library.
class PhysMemReader {
 public:
  virtual ~PhysMemReader() = default;
  virtual zx::result<const void*> PhysToPtr(uintptr_t phys, size_t length) = 0;
};

//
// Functions below exposed for testing.
//

// Ensure the checksum of the given block of code is valid.
bool AcpiChecksumValid(const void* buf, size_t len);

// Calculate a checksum of the given range of memory.
uint8_t AcpiChecksum(const void* _buf, size_t len);

// Validate the RSDT / XSDT tables.
zx::result<const AcpiRsdt*> ValidateRsdt(PhysMemReader& reader, uint32_t rsdt_pa,
                                         size_t* num_tables);
zx::result<const AcpiXsdt*> ValidateXsdt(PhysMemReader& reader, uint64_t xsdt_pa,
                                         size_t* num_tables);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_H_
