// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/acpi_lite.h"

#include <inttypes.h>
#include <trace.h>
#include <zircon/compiler.h>

#include <ktl/algorithm.h>
#include <pretty/hexdump.h>
#include <vm/physmap.h>

#define LOCAL_TRACE 0

namespace acpi_lite {
namespace {

// Maximum supported RSDP table size.
//
// We assume RSDP values larger than this size are invalid.
constexpr size_t kMaxRsdpSize = 4096;

// Convert physical addresses to virtual addresses using Zircon's standard conversion
// functions.
class ZirconPhysmemReader final : public PhysMemReader {
 public:
  constexpr ZirconPhysmemReader() = default;

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) final {
    // We don't support the 0 physical address or 0-length ranges.
    if (length == 0 || phys == 0) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }

    // Get the last byte of the specified range, ensuring we don't wrap around the address
    // space.
    uintptr_t phys_end;
    if (add_overflow(phys, length - 1, &phys_end)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    // Ensure that both "phys" and "phys + length - 1" have valid addresses.
    //
    // The Zircon physmap is contiguous, so we don't have to worry about intermediate addresses.
    if (!is_physmap_phys_addr(phys) || !is_physmap_phys_addr(phys_end)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    return zx::success(paddr_to_physmap(phys));
  }
};

// Perform a two-phase PhysToPtr conversion:
//
//   1. We first read a fixed-sized header.
//   2. We next call the function |bytes_to_read|, passing in this header.
//   3. We next map in the number of bytes indicated by the function.
//
// This allows us to handle the common use-case where the number of bytes that need
// to be accessed at a particular address cannot be determined until we first read
// a header at that address.
//
// |bytes_to_read| is a function of type "size_t F(const T&)". We use a lambda
// (and not a member pointer, for example) because the length does not have a fixed
// name and/or may be inside a nested struct.
template <typename T, typename F>
zx::status<const T*> PhysToPtrDynamicSize(PhysMemReader& reader, uintptr_t phys, F bytes_to_read) {
  // Try and read the header.
  zx::status<const void*> result = reader.PhysToPtr(phys, sizeof(T));
  if (result.is_error()) {
    return result.take_error();
  }

  // Get the number of bytes the full structure needs, as determined by its header.
  size_t bytes_needed = ktl::max(
      static_cast<size_t>(bytes_to_read(static_cast<const T*>(result.value()))), sizeof(T));
  result = reader.PhysToPtr(phys, bytes_needed);
  if (result.is_error()) {
    return result.take_error();
  }

  return zx::success(static_cast<const T*>(result.value()));
}

// Calculate a checksum of the given range of memory.
//
// The checksum is valid if the sum of bytes mod 256 == 0.
bool AcpiChecksumValid(const void* _buf, size_t len) {
  uint8_t c = 0;

  const uint8_t* buf = static_cast<const uint8_t*>(_buf);
  for (size_t i = 0; i < len; i++) {
    c = static_cast<uint8_t>(c + buf[i]);
  }

  return c == 0;
}

bool ValidateRsdp(const AcpiRsdpV2* rsdp, size_t max_length) {
  // Ensure the range is large enough to handle a RSDP header.
  if (max_length < sizeof(AcpiRsdp)) {
    return false;
  }

  // Verify the RSDP signature.
  if (memcmp(ACPI_RSDP_SIG, &rsdp->v1.sig, ACPI_RSDP_SIG_LENGTH) != 0) {
    return false;
  }

  // Validate the checksum on the V1 header.
  if (!AcpiChecksumValid(&rsdp->v1, sizeof(AcpiRsdp))) {
    return false;
  }

  // If the RSDP header states that we are a full V2 structure, also verify the additional data.
  if (rsdp->v1.revision >= 2) {
    // Ensure we have enough bytes for the entire V2 header.
    if (max_length < sizeof(AcpiRsdpV2)) {
      return false;
    }

    // Ensure the length looks reasonable.
    if (rsdp->length < sizeof(AcpiRsdpV2) || rsdp->length > max_length) {
      return false;
    }

    // Verify the extended checksump.
    if (!AcpiChecksumValid(rsdp, rsdp->length)) {
      return false;
    }
  }

  return true;
}

// Search for a valid RSDP in the BIOS read-only memory space in [0xe0000..0xfffff],
// on 16 byte boundaries.
//
// Return 0 if no RSDP found.
//
// Reference: ACPI v6.3, Section 5.2.5.1
zx::status<const AcpiRsdpV2*> FindRsdpPc(PhysMemReader& reader) {
  // Get a virtual address for the read-only BIOS range.
  zx::status<const void*> maybe_bios_section =
      reader.PhysToPtr(kBiosReadOnlyAreaStart, kBiosReadOnlyAreaLength);
  if (maybe_bios_section.is_error()) {
    return maybe_bios_section.take_error();
  }
  auto* bios_section = static_cast<const uint8_t*>(maybe_bios_section.value());

  // Try every 16-byte offset from 0xe0'0000.
  for (size_t offset = 0x0; offset < kBiosReadOnlyAreaLength; offset += 16) {
    const auto rsdp = reinterpret_cast<const AcpiRsdpV2*>(bios_section + offset);
    if (ValidateRsdp(rsdp, /*max_length=*/kBiosReadOnlyAreaLength - offset)) {
      return zx::success(reinterpret_cast<const AcpiRsdpV2*>(rsdp));
    }
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

bool ValidateSdt(const AcpiRsdtXsdt& sdt, size_t* num_tables, bool* xsdt) {
  LTRACEF("%p\n", &sdt);

  // Check the signature to see if it's a RSDT or XSDT.
  if (!memcmp(sdt.header.sig, ACPI_XSDT_SIG, ACPI_XSDT_SIG_LENGTH)) {
    *xsdt = true;
  } else if (!memcmp(sdt.header.sig, ACPI_RSDT_SIG, ACPI_RSDT_SIG_LENGTH)) {
    *xsdt = false;
  } else {
    return false;
  }

  // Ensure the length field is reasonable.
  if (sdt.header.length < sizeof(AcpiSdtHeader) || sdt.header.length > 4096) {
    return false;
  }

  // Ensure this is a revision we understand.
  if (sdt.header.revision != 1) {
    return false;
  }

  // Validate SDT checksum.
  if (!AcpiChecksumValid(&sdt, sdt.header.length)) {
    return false;
  }

  // Compute the number of tables we have.
  *num_tables =
      (sdt.header.length - sizeof(AcpiSdtHeader)) / (*xsdt ? sizeof(uint64_t) : sizeof(uint32_t));

  return true;
}

}  // namespace

const AcpiSdtHeader* AcpiParser::GetTableAtIndex(size_t index) const {
  if (index >= num_tables_) {
    return nullptr;
  }

  // Get the physical address for the index'th table.
  zx_paddr_t pa = xsdt_ ? sdt_->addr64[index] : sdt_->addr32[index];

  // Map it in.
  return PhysToPtrDynamicSize<AcpiSdtHeader>(*reader_, pa,
                                             [](const AcpiSdtHeader* v) { return v->length; })
      .value_or(nullptr);
}

const AcpiSdtHeader* AcpiParser::GetTableBySignature(const char* sig) const {
  for (size_t i = 0; i < num_tables_; i++) {
    const AcpiSdtHeader* header = GetTableAtIndex(i);
    if (!header) {
      continue;
    }

    // Continue searching if the header doesn't match.
    if (memcmp(sig, header->sig, sizeof(header->sig)) != 0) {
      continue;
    }

    // If the checksum is invalid, keep searching.
    if (!AcpiChecksumValid(header, header->length)) {
      continue;
    }

    return header;
  }

  return nullptr;
}

zx::status<AcpiParser> AcpiParser::Init(zx_paddr_t rsdp_pa) {
  // AcpiParser requires a ZirconPhysmemReader instance that outlives
  // it. We share a single static instance for all AcpiParser instances.
  static ZirconPhysmemReader reader;

  return Init(reader, rsdp_pa);
}

zx::status<const AcpiRsdpV2*> FindRsdp(PhysMemReader& physmem_reader, zx_paddr_t rsdp_pa) {
  // If the user gave us an explicit RSDP, just use that directly.
  if (rsdp_pa != 0) {
    zx::status<const AcpiRsdpV2*> result = PhysToPtrDynamicSize<AcpiRsdpV2>(
        physmem_reader, rsdp_pa, [](const AcpiRsdpV2* v) { return v->length; });
    if (result.is_error()) {
      dprintf(INFO, "ACPI LITE: failed to map RSDP address %#" PRIxPTR " to virtual\n", rsdp_pa);
      return result.take_error();
    }

    return result;
  }

  // Otherwise, attempt to find it in a platform-specific way.
#if __x86_64__
  {
    zx::status<const AcpiRsdpV2*> result = FindRsdpPc(physmem_reader);
    if (result.is_ok()) {
      return result.take_value();
    }
    dprintf(INFO, "ACPI LITE: couldn't find ACPI RSDP in BIOS area\n");
  }
#endif

  return zx::error(ZX_ERR_NOT_FOUND);
}

// Find the pointer to either the RSDT or XSDT.
zx::status<const AcpiRsdtXsdt*> FindRsdtOrXsdt(PhysMemReader& physmem_reader,
                                               const AcpiRsdpV2* rsdp) {
  // Prefer using the XSDT if it is valid.
  if (rsdp->v1.revision >= 2) {
    // Try XSDT.
    zx::status<const AcpiRsdtXsdt*> sdt = PhysToPtrDynamicSize<AcpiRsdtXsdt>(
        physmem_reader, rsdp->xsdt_address, [](const AcpiRsdtXsdt* v) { return v->header.length; });
    if (sdt.is_ok()) {
      return sdt.take_value();
    }
    dprintf(INFO, "ACPI LITE: RSDP points to invalid XSDT. Falling back to RSDT.\n");
  }

  // Fall back to the RSDT.
  return PhysToPtrDynamicSize<AcpiRsdtXsdt>(physmem_reader, rsdp->v1.rsdt_address,
                                            [](const AcpiRsdtXsdt* v) { return v->header.length; });
}

zx::status<AcpiParser> AcpiParser::Init(PhysMemReader& physmem_reader, zx_paddr_t rsdp_pa) {
  LTRACEF("passed in rsdp %#" PRIxPTR "\n", rsdp_pa);

  // Get the RSDP.
  zx::status<const AcpiRsdpV2*> rsdp = FindRsdp(physmem_reader, rsdp_pa);
  if (rsdp.is_error()) {
    return rsdp.take_error();
  }

  // Attempt to validate the RSDP.
  if (!ValidateRsdp(rsdp.value(), kMaxRsdpSize)) {
    dprintf(INFO, "ACPI LITE: Could not validate RSDP structure.\n");
    if (DPRINTF_ENABLED_FOR_LEVEL(SPEW)) {
      hexdump(rsdp.value(), sizeof(AcpiRsdpV2));
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  dprintf(SPEW, "ACPI LITE: Valid RSDP found at %#lx\n", rsdp_pa);

  // Find the pointer to either the RSDT or XSDT.
  //
  // We prefer using the XSDT, falling back to the RSDT if no XSDT is available or it is invalid.
  zx::status<const AcpiRsdtXsdt*> sdt = FindRsdtOrXsdt(physmem_reader, rsdp.value());
  if (sdt.is_error()) {
    dprintf(INFO, "ACPI LITE: Could not map valid RSDT.\n");
    return sdt.take_error();
  }

  // Validate the SDT.
  size_t num_tables;
  bool xsdt;
  if (!ValidateSdt(*sdt.value(), &num_tables, &xsdt)) {
    dprintf(INFO, "ACPI LITE: Invalid RSDT/XSDT structure.\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  dprintf(SPEW, "ACPI LITE: RSDT/XSDT valid, %zu tables\n", num_tables);

  // Create the table.
  AcpiParser parser(physmem_reader, *sdt.value(), num_tables, xsdt);

  if (LOCAL_TRACE) {
    parser.DumpTables();
  }

  return zx::ok(parser);
}

void AcpiParser::DumpTables() const {
  DEBUG_ASSERT(sdt_ != nullptr);

  printf("root table:\n");
  hexdump(sdt_, sdt_->header.length);

  // walk the table list
  for (size_t i = 0; i < num_tables_; i++) {
    const auto header = GetTableAtIndex(i);
    if (!header) {
      continue;
    }

    printf("table %zu: '%c%c%c%c' len %u\n", i, header->sig[0], header->sig[1], header->sig[2],
           header->sig[3], header->length);
    hexdump(header, header->length);
  }
}

zx_status_t AcpiParser::EnumerateMadtEntries(const uint8_t search_type,
                                             const MadtEntryCallback& callback) const {
  const AcpiMadtTable* madt =
      reinterpret_cast<const AcpiMadtTable*>(GetTableBySignature(ACPI_MADT_SIG));
  if (!madt) {
    return ZX_ERR_NOT_FOUND;
  }

  // bytewise array of the same table
  const uint8_t* madt_array = reinterpret_cast<const uint8_t*>(madt);

  // walk the table off the end of the header, looking for the requested type
  size_t off = sizeof(*madt);
  while (off < madt->header.length) {
    uint8_t type = madt_array[off];
    uint8_t length = madt_array[off + 1];

    if (type == search_type) {
      callback(static_cast<const void*>(&madt_array[off]), length);
    }

    off += length;
  }

  return ZX_OK;
}

}  // namespace acpi_lite
