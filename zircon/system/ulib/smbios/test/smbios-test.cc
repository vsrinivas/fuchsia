// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/smbios/smbios.h>
#include <fbl/array.h>
#include <memory>
#include <zxtest/zxtest.h>

namespace {

uint8_t ComputeChecksum(const uint8_t* data, size_t len) {
  unsigned int sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += data[i];
  }
  return static_cast<uint8_t>(sum);
}
smbios::EntryPoint2_1 CreateFakeEntryPoint(const fbl::Array<uint8_t>& structs,
                                           uint16_t structures_count) {
  smbios::EntryPoint2_1 ep = {
    .anchor_string = {'_', 'S', 'M', '_'},
    .checksum = 0,
    .length = sizeof(smbios::EntryPoint2_1),
    .major_ver = 2,
    .minor_ver = 1,
    .max_struct_size = 256,
    .ep_rev = 0,
    .formatted_area = {},
    .intermediate_anchor_string = {'_', 'D', 'M', 'I', '_'},
    .intermediate_checksum = 0,
    .struct_table_length = static_cast<uint16_t>(structs.size()),
    .struct_table_phys = 0x1000, // Fake physical address
    .struct_count = structures_count,
    .bcd_rev = 0x21
  };

  // The specification defines the offsets for this checksum
  ep.intermediate_checksum = static_cast<uint8_t>(
      256u - ComputeChecksum(reinterpret_cast<const uint8_t*>(&ep) + 0x10, 0xf));
  ep.checksum = static_cast<uint8_t>(
      256 - ComputeChecksum(reinterpret_cast<const uint8_t*>(&ep), sizeof(ep)));

  return ep;
}

#define BIOS_STRING1 "string1"
#define BIOS_STRING2 "string2"

// Create fake SMBIOSv2.1 structures
void CreateFakeSmbios(smbios::EntryPoint2_1* ep, fbl::Array<uint8_t>* structs) {
  constexpr uint16_t kNumStructures = 2;
  // A double null terminates the string table
  const char bios_info_strings[] = BIOS_STRING1 "\0" BIOS_STRING2 "\0";
  const size_t bios_info_size =
      sizeof(smbios::BiosInformationStruct2_0) + sizeof(bios_info_strings);
  const char sys_info_strings[] = "\0";
  const size_t sys_info_size =
      sizeof(smbios::SystemInformationStruct2_1) + sizeof(sys_info_strings);
  const size_t struct_data_size = bios_info_size + sys_info_size;

  fbl::Array<uint8_t> struct_data(new uint8_t[struct_data_size](), struct_data_size);
  uint8_t* next_struct_data = struct_data.data();

  smbios::BiosInformationStruct2_0 bios_info = {};
  bios_info.hdr.type = smbios::StructType::BiosInfo;
  bios_info.hdr.length = sizeof(bios_info);
  bios_info.hdr.handle = 0;
  memcpy(next_struct_data, &bios_info, sizeof(bios_info));
  next_struct_data += sizeof(bios_info);
  memcpy(next_struct_data, bios_info_strings, sizeof(bios_info_strings));
  next_struct_data += sizeof(bios_info_strings);

  smbios::SystemInformationStruct2_1 sys_info = {};
  sys_info.hdr.type = smbios::StructType::SystemInfo;
  sys_info.hdr.length = sizeof(sys_info);
  sys_info.hdr.handle = 1;
  memcpy(next_struct_data, &sys_info, sizeof(sys_info));
  next_struct_data += sizeof(sys_info);
  memcpy(next_struct_data, sys_info_strings, sizeof(sys_info_strings));
  next_struct_data += sizeof(sys_info_strings);

  ASSERT_EQ(struct_data.data() + struct_data_size, next_struct_data);

  *ep = CreateFakeEntryPoint(struct_data, kNumStructures);
  ASSERT_TRUE(ep->IsValid());
  *structs = std::move(struct_data);
}

TEST(SmbiosTestCase, WalkStructs) {
  smbios::EntryPoint2_1 ep;
  fbl::Array<uint8_t> structs;
  ASSERT_NO_FATAL_FAILURES(CreateFakeSmbios(&ep, &structs));

  bool tables_seen[2] = {};
  auto walk_cb = [&ep, &tables_seen](smbios::SpecVersion version, const smbios::Header* h,
                                     const smbios::StringTable& st) {
    EXPECT_EQ(version.major_ver, ep.major_ver);
    EXPECT_EQ(version.minor_ver, ep.minor_ver);
    switch (h->type) {
      case smbios::StructType::BiosInfo:
      case smbios::StructType::SystemInfo: {
        EXPECT_FALSE(tables_seen[static_cast<size_t>(h->type)]);
        tables_seen[static_cast<size_t>(h->type)] = true;
        break;
      }
      default:
        ADD_FAILURE("Saw unexpected header type");
    }
    return ZX_OK;
  };
  ASSERT_OK(ep.WalkStructs(reinterpret_cast<uintptr_t>(structs.data()), walk_cb));
  ASSERT_TRUE(tables_seen[0]);
  ASSERT_TRUE(tables_seen[1]);
}

TEST(SmbiosTestCase, WalkStructsEarlyStop) {
  smbios::EntryPoint2_1 ep;
  fbl::Array<uint8_t> structs;
  ASSERT_NO_FATAL_FAILURES(CreateFakeSmbios(&ep, &structs));

  auto walk_cb = [](smbios::SpecVersion version, const smbios::Header* h,
                    const smbios::StringTable& st) {
    switch (h->type) {
      case smbios::StructType::BiosInfo:
        return ZX_ERR_STOP;
      case smbios::StructType::SystemInfo: {
        ADD_FAILURE("Iterator saw SystemInfo");
        break;
      }
      default:
        ADD_FAILURE("Saw unexpected header type");
    }
    return ZX_OK;
  };
  ASSERT_OK(ep.WalkStructs(reinterpret_cast<uintptr_t>(structs.data()), walk_cb));
}

TEST(SmbiosTestCase, GetString) {
  smbios::EntryPoint2_1 ep;
  fbl::Array<uint8_t> structs;
  ASSERT_NO_FATAL_FAILURES(CreateFakeSmbios(&ep, &structs));

  auto walk_cb = [](smbios::SpecVersion version, const smbios::Header* h,
                    const smbios::StringTable& st) {
    switch (h->type) {
      case smbios::StructType::BiosInfo: {
        const char* str = nullptr;
        EXPECT_OK(st.GetString(0, &str));
        EXPECT_STR_EQ("<null>", str);
        EXPECT_OK(st.GetString(1, &str));
        EXPECT_STR_EQ(BIOS_STRING1, str);
        EXPECT_OK(st.GetString(2, &str));
        EXPECT_STR_EQ(BIOS_STRING2, str);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, st.GetString(3, &str));
        break;
      }
      case smbios::StructType::SystemInfo: {
        const char* str = nullptr;
        EXPECT_OK(st.GetString(0, &str));
        EXPECT_STR_EQ("<null>", str);
        EXPECT_EQ(ZX_ERR_NOT_FOUND, st.GetString(1, &str));
        break;
      }
      default:
        ADD_FAILURE("Saw unexpected header type");
    }
    return ZX_OK;
  };
  ASSERT_OK(ep.WalkStructs(reinterpret_cast<uintptr_t>(structs.data()), walk_cb));
}

TEST(SmbiosTestCase, BaseboardInformationTruncations) {
  static_assert(alignof(smbios::BaseboardInformationStruct) == 1);
  uint8_t raw[23] = {};
  auto baseboard = reinterpret_cast<smbios::BaseboardInformationStruct*>(raw);

  baseboard->hdr.type = smbios::StructType::Baseboard;
  baseboard->hdr.length = 8;
  ASSERT_FALSE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_FALSE(baseboard->feature_flags().has_value());
  ASSERT_FALSE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_FALSE(baseboard->chassis_handle().has_value());
  ASSERT_FALSE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 9;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_FALSE(baseboard->feature_flags().has_value());
  ASSERT_FALSE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_FALSE(baseboard->chassis_handle().has_value());
  ASSERT_FALSE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 10;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_TRUE(baseboard->feature_flags().has_value());
  ASSERT_FALSE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_FALSE(baseboard->chassis_handle().has_value());
  ASSERT_FALSE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 11;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_TRUE(baseboard->feature_flags().has_value());
  ASSERT_TRUE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_FALSE(baseboard->chassis_handle().has_value());
  ASSERT_FALSE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 13;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_TRUE(baseboard->feature_flags().has_value());
  ASSERT_TRUE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_TRUE(baseboard->chassis_handle().has_value());
  ASSERT_FALSE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 14;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_TRUE(baseboard->feature_flags().has_value());
  ASSERT_TRUE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_TRUE(baseboard->chassis_handle().has_value());
  ASSERT_TRUE(baseboard->board_type().has_value());
  ASSERT_FALSE(baseboard->contained_object_handles_count().has_value());

  baseboard->hdr.length = 15;
  ASSERT_TRUE(baseboard->asset_tag_str_idx().has_value());
  ASSERT_TRUE(baseboard->feature_flags().has_value());
  ASSERT_TRUE(baseboard->location_in_chassis_str_idx().has_value());
  ASSERT_TRUE(baseboard->chassis_handle().has_value());
  ASSERT_TRUE(baseboard->board_type().has_value());
  ASSERT_TRUE(baseboard->contained_object_handles_count().has_value());
}

}  // namespace
