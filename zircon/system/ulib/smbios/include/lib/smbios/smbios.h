// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/function.h>
#include <stdint.h>
#include <zircon/types.h>

#ifndef _KERNEL
#include <optional>
#endif

#define SMBIOS2_ANCHOR "_SM_"
#define SMBIOS2_INTERMEDIATE_ANCHOR "_DMI_"
#define SMBIOS3_ANCHOR "_SM3_"

namespace smbios {

enum class StructType : uint8_t {
  BiosInfo = 0,
  SystemInfo = 1,
  Baseboard = 2,
  SystemEnclosure = 3,
  Processor = 4,
  MemoryController = 5,
  MemoryModule = 6,
  Cache = 7,
  PortConnector = 8,
  SystemSlots = 9,
  OnBoardDevices = 10,
  OemStrings = 11,
  SystemConfigOptions = 12,
  BiosLanguage = 13,

  EndOfTable = 127,
};

// SMBIOS common struct header
struct Header {
  StructType type;
  uint8_t length;
  uint16_t handle;
} __PACKED;
static_assert(sizeof(Header) == 4, "");

// Utility for working with the table of null-terminated strings after each
// struct.
class StringTable {
 public:
  StringTable();
  ~StringTable();

  // Construct a StringTable from a header and a max possible length.  The
  // length includes the formatted portion (h->length).
  zx_status_t Init(const Header* h, size_t max_struct_len);

  // Return the length of the StringTable, in bytes, including terminating NUL.
  size_t length() const { return length_; }

  // This operation is slow, and indexed from 1.  |*out| is always assigned a
  // null-terminated string, even on error.
  zx_status_t GetString(size_t idx, const char** out) const;
  // Convenience version that does not identify the error
  const char* GetString(size_t idx) const {
    const char* p;
    GetString(idx, &p);
    return p;
  }

  void Dump() const;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(StringTable);

  const char* start_ = nullptr;
  size_t length_ = 0;
};

// Utility for comparing specification versions.  Used to select which version
// of the spec to use for interpreting a struct.
struct SpecVersion {
  SpecVersion(uint8_t major, uint8_t minor) : major_ver(major), minor_ver(minor) {}

  // Returns true if this support for at least the queried version.
  bool IncludesVersion(uint8_t spec_major_ver, uint8_t spec_minor_ver) const;

  uint8_t major_ver;
  uint8_t minor_ver;
};

enum class EntryPointVersion {
  Unknown,
  V2_1,
  V3_0,
};

// Callback used when walking the structure table.
// If it returns ZX_ERR_STOP, the walk is aborted and returns ZX_OK
// If it returns ZX_OK or ZX_ERR_NEXT, the walk is continued.
// For any other return, the walk is aborted and returns the error returned by
// the callback.
using StructWalkCallback =
    fbl::InlineFunction<zx_status_t(SpecVersion version, const Header* h, const StringTable& st),
                        fbl::kDefaultInlineCallableSize>;

// System structure identifying where the SMBIOS structs are in memory.
struct EntryPoint2_1 {
  uint8_t anchor_string[4];  // _SM_
  uint8_t checksum;
  uint8_t length;

  // SMBIOS specification revision
  uint8_t major_ver;
  uint8_t minor_ver;

  uint16_t max_struct_size;

  uint8_t ep_rev;             // Should be 0x00 for version SMBIOS 2.1 entry point
  uint8_t formatted_area[5];  // Should be all 0x00 for ver 2.1

  uint8_t intermediate_anchor_string[5];  // _DMI_
  uint8_t intermediate_checksum;

  uint16_t struct_table_length;
  uint32_t struct_table_phys;
  uint16_t struct_count;

  uint8_t bcd_rev;  // Should be 0x21

  bool IsValid() const;

  // Walk the known SMBIOS structures, assuming they are mapped at struct_table_virt.  The
  // callback will be called once for each structure found.
  zx_status_t WalkStructs(uintptr_t struct_table_virt, StructWalkCallback cb) const;

  SpecVersion version() const { return SpecVersion(major_ver, minor_ver); }

  void Dump() const;
} __PACKED;
static_assert(sizeof(EntryPoint2_1) == 0x1f, "");

struct BiosInformationStruct2_0 {
  Header hdr;

  uint8_t vendor_str_idx;
  uint8_t bios_version_str_idx;
  uint16_t bios_starting_address_segment;
  uint8_t bios_release_date_str_idx;
  uint8_t bios_rom_size;
  uint64_t bios_characteristics;
  uint8_t bios_characteristics_ext[0];

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(BiosInformationStruct2_0) == 0x12, "");

struct BiosInformationStruct2_4 {
  Header hdr;

  uint8_t vendor_str_idx;
  uint8_t bios_version_str_idx;
  uint16_t bios_starting_address_segment;
  uint8_t bios_release_date_str_idx;
  uint8_t bios_rom_size;
  uint64_t bios_characteristics;
  uint16_t bios_characteristics_ext;

  uint8_t bios_major_release;
  uint8_t bios_minor_release;
  uint8_t ec_major_release;
  uint8_t ec_minor_release;

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(BiosInformationStruct2_4) == 0x18, "");

struct SystemInformationStruct2_0 {
  Header hdr;

  uint8_t manufacturer_str_idx;
  uint8_t product_name_str_idx;
  uint8_t version_str_idx;
  uint8_t serial_number_str_idx;

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(SystemInformationStruct2_0) == 0x8, "");

struct SystemInformationStruct2_1 {
  Header hdr;

  uint8_t manufacturer_str_idx;
  uint8_t product_name_str_idx;
  uint8_t version_str_idx;
  uint8_t serial_number_str_idx;

  uint8_t uuid[16];
  uint8_t wakeup_type;

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(SystemInformationStruct2_1) == 0x19, "");

struct SystemInformationStruct2_4 {
  Header hdr;

  uint8_t manufacturer_str_idx;
  uint8_t product_name_str_idx;
  uint8_t version_str_idx;
  uint8_t serial_number_str_idx;

  uint8_t uuid[16];
  uint8_t wakeup_type;

  uint8_t sku_number_str_idx;
  uint8_t family_str_idx;

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(SystemInformationStruct2_4) == 0x1b, "");

#ifndef _KERNEL

// Safe accessor for fields which may be out of the bounds of a structure.
// Some SMBIOS structures can be truncated at several different points, and
// this allows users to safetly read them.
//
// StructType must refer to a structure with a member "hdr" of type "Header".
template <typename StructType, typename FieldType>
std::optional<FieldType> ReadOptionalField(const StructType* s, FieldType StructType::*field) {
  const auto end = reinterpret_cast<const std::byte*>(s) + s->hdr.length;
  const auto field_end = reinterpret_cast<const std::byte*>(&(s->*field) + 1);
  if (field_end > end) {
    return {};
  }

  // Use memcpy because some fields are misaligned and direct access to
  // misaligned fields is undefined behavior.  It should get optimized away.
  FieldType value;
  memcpy(&value, &(s->*field), sizeof(value));
  return value;
}

struct BaseboardInformationStruct {
  Header hdr;
  uint8_t manufacturer_str_idx;
  uint8_t product_name_str_idx;
  uint8_t version_str_idx;
  uint8_t serial_number_str_idx;

  // All of these "unsafe" fields should be accessed using the accesor methods
  // below.  The fields are not marked private, since that would make this
  // struct not standard layout.
  uint8_t unsafe_asset_tag_str_idx;
  uint8_t unsafe_feature_flags;
  uint8_t unsafe_location_in_chassis_str_idx;
  uint16_t unsafe_chassis_handle;

  uint8_t unsafe_board_type;
  uint8_t unsafe_contained_object_handles_count;
  uint16_t contained_object_handles[];

  std::optional<uint8_t> asset_tag_str_idx() const {
    return ReadOptionalField(this, &BaseboardInformationStruct::unsafe_asset_tag_str_idx);
  }

  std::optional<uint8_t> feature_flags() const {
    return ReadOptionalField(this, &BaseboardInformationStruct::unsafe_feature_flags);
  }
  std::optional<uint8_t> location_in_chassis_str_idx() const {
    return ReadOptionalField(this, &BaseboardInformationStruct::unsafe_location_in_chassis_str_idx);
  }
  std::optional<uint16_t> chassis_handle() const {
    return ReadOptionalField(this, &BaseboardInformationStruct::unsafe_chassis_handle);
  }
  std::optional<uint8_t> board_type() const {
    return ReadOptionalField(this, &BaseboardInformationStruct::unsafe_board_type);
  }
  std::optional<uint8_t> contained_object_handles_count() const {
    return ReadOptionalField(this,
                             &BaseboardInformationStruct::unsafe_contained_object_handles_count);
  }

  void Dump(const StringTable& st) const;
} __PACKED;
static_assert(sizeof(BaseboardInformationStruct) == 0xf);
static_assert(std::is_standard_layout_v<BaseboardInformationStruct>);

#endif

}  // namespace smbios
