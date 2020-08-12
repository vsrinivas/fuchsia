// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <gpt/guid.h>

namespace gpt {

#define GUID_NAMETAB(name) \
  GuidProperties(GUID_##name##_NAME, uuid::Uuid(GUID_##name##_VALUE), PartitionScheme::kLegacy)

// clang-format off
std::array<GuidProperties, kKnownGuidEntries> const KnownGuid::nametab_ = {{
    // Legacy GUID definitions - each entry has its own type GUID.
    GUID_NAMETAB(EMPTY),
    GUID_NAMETAB(EFI),
    GUID_NAMETAB(SYSTEM),
    GUID_NAMETAB(DATA),
    GUID_NAMETAB(INSTALL),
    GUID_NAMETAB(BLOB),
    GUID_NAMETAB(FVM),
    GUID_NAMETAB(ZIRCON_A),
    GUID_NAMETAB(ZIRCON_B),
    GUID_NAMETAB(ZIRCON_R),
    GUID_NAMETAB(SYS_CONFIG),
    GUID_NAMETAB(FACTORY_CONFIG),
    GUID_NAMETAB(BOOTLOADER),
    GUID_NAMETAB(TEST),
    GUID_NAMETAB(VBMETA_A),
    GUID_NAMETAB(VBMETA_B),
    GUID_NAMETAB(VBMETA_R),
    GUID_NAMETAB(ABR_META),
    GUID_NAMETAB(CROS_KERNEL),
    GUID_NAMETAB(CROS_ROOTFS),
    GUID_NAMETAB(CROS_RESERVED),
    GUID_NAMETAB(CROS_FIRMWARE),
    GUID_NAMETAB(CROS_DATA),
    GUID_NAMETAB(BIOS),
    GUID_NAMETAB(EMMC_BOOT1),
    GUID_NAMETAB(EMMC_BOOT2),
    GUID_NAMETAB(LINUX_FILESYSTEM_DATA),

    // New GUID definitions - slotted partitions share a type GUID.
    GuidProperties(GPT_BOOTLOADER_A_NAME, GPT_BOOTLOADER_ABR_TYPE_GUID, PartitionScheme::kNew),
    GuidProperties(GPT_BOOTLOADER_B_NAME, GPT_BOOTLOADER_ABR_TYPE_GUID, PartitionScheme::kNew),
    GuidProperties(GPT_BOOTLOADER_R_NAME, GPT_BOOTLOADER_ABR_TYPE_GUID, PartitionScheme::kNew),
    GuidProperties(GPT_DURABLE_NAME,      GPT_DURABLE_TYPE_GUID,        PartitionScheme::kNew),
    GuidProperties(GPT_DURABLE_BOOT_NAME, GPT_DURABLE_BOOT_TYPE_GUID,   PartitionScheme::kNew),
    GuidProperties(GPT_FACTORY_NAME,      GPT_FACTORY_TYPE_GUID,        PartitionScheme::kNew),
    GuidProperties(GPT_FACTORY_BOOT_NAME, GPT_FACTORY_BOOT_TYPE_GUID,   PartitionScheme::kNew),
    GuidProperties(GPT_FVM_NAME,          GPT_FVM_TYPE_GUID,            PartitionScheme::kNew),
    GuidProperties(GPT_VBMETA_A_NAME,     GPT_VBMETA_ABR_TYPE_GUID,     PartitionScheme::kNew),
    GuidProperties(GPT_VBMETA_B_NAME,     GPT_VBMETA_ABR_TYPE_GUID,     PartitionScheme::kNew),
    GuidProperties(GPT_VBMETA_R_NAME,     GPT_VBMETA_ABR_TYPE_GUID,     PartitionScheme::kNew),
    GuidProperties(GPT_ZIRCON_A_NAME,     GPT_ZIRCON_ABR_TYPE_GUID,     PartitionScheme::kNew),
    GuidProperties(GPT_ZIRCON_B_NAME,     GPT_ZIRCON_ABR_TYPE_GUID,     PartitionScheme::kNew),
    GuidProperties(GPT_ZIRCON_R_NAME,     GPT_ZIRCON_ABR_TYPE_GUID,     PartitionScheme::kNew),
}};
// clang-format on

std::list<const GuidProperties*> KnownGuid::Find(std::optional<std::string_view> name,
                                                 std::optional<uuid::Uuid> type_guid,
                                                 std::optional<PartitionScheme> scheme) {
  std::list<const GuidProperties*> result;
  for (const GuidProperties& properties : nametab_) {
    if ((!name || *name == properties.name()) &&
        (!type_guid || *type_guid == properties.type_guid()) &&
        (!scheme || *scheme == properties.scheme())) {
      result.push_back(&properties);
    }
  }
  return result;
}

namespace {

// Returns the longest common prefix between |a| and |b|.
std::string_view CommonPrefix(std::string_view a, std::string_view b) {
  const int end = std::min(a.size(), b.size());
  for (int i = 0; i < end; ++i) {
    if (a[i] != b[i]) {
      return std::string_view(a.data(), i);
    }
  }
  return std::string_view(a.data(), end);
}

}  // namespace

std::string KnownGuid::TypeDescription(const uuid::Uuid& type_guid) {
  auto matches = Find(std::nullopt, type_guid, std::nullopt);

  // No matches: return empty string.
  if (matches.empty()) {
    return "";
  }

  // One match: just return the partition name directly.
  if (matches.size() == 1) {
    return std::string(matches.front()->name());
  }

  // Multiple matches: use the longest common prefix.
  std::string_view prefix = matches.front()->name();
  for (auto iter = ++matches.begin(); iter != matches.end(); ++iter) {
    prefix = CommonPrefix(prefix, (*iter)->name());
  }

  // Return "<prefix>*".
  return std::string(prefix) + "*";
}

std::string KnownGuid::TypeDescription(const uint8_t* type_guid) {
  return TypeDescription(uuid::Uuid(type_guid));
}

}  // namespace gpt
