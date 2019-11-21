// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <gpt/guid.h>

namespace gpt {

// GUID_*_VALUE are brace-enclosed values {1, 2,..}. Function arguments
// cannot be brace-enclosed initializer. So here we convert VALUE to a
// constexpr
#define GUID_MAKE_ARRAY(name) \
  constexpr uint8_t GUID_##name##_ARRAY[GPT_GUID_LEN] = GUID_##name##_VALUE

GUID_MAKE_ARRAY(EMPTY);
GUID_MAKE_ARRAY(EFI);
GUID_MAKE_ARRAY(SYSTEM);
GUID_MAKE_ARRAY(DATA);
GUID_MAKE_ARRAY(INSTALL);
GUID_MAKE_ARRAY(BLOB);
GUID_MAKE_ARRAY(FVM);
GUID_MAKE_ARRAY(ZIRCON_A);
GUID_MAKE_ARRAY(ZIRCON_B);
GUID_MAKE_ARRAY(ZIRCON_R);
GUID_MAKE_ARRAY(SYS_CONFIG);
GUID_MAKE_ARRAY(FACTORY_CONFIG);
GUID_MAKE_ARRAY(BOOTLOADER);
GUID_MAKE_ARRAY(TEST);
GUID_MAKE_ARRAY(VBMETA_A);
GUID_MAKE_ARRAY(VBMETA_B);
GUID_MAKE_ARRAY(CROS_KERNEL);
GUID_MAKE_ARRAY(CROS_ROOTFS);
GUID_MAKE_ARRAY(CROS_RESERVED);
GUID_MAKE_ARRAY(CROS_FIRMWARE);
GUID_MAKE_ARRAY(CROS_DATA);
GUID_MAKE_ARRAY(BIOS);
GUID_MAKE_ARRAY(EMMC_BOOT1);
GUID_MAKE_ARRAY(EMMC_BOOT2);
GUID_MAKE_ARRAY(LINUX_FILESYSTEM_DATA);

#define GUID_NAMETAB(name) \
  GuidProperties(GUID_##name##_NAME, GUID_##name##_STRING, GUID_##name##_ARRAY)

// clang-format off
std::array<GuidProperties, kKnownGuidEntries> const KnownGuid::nametab_ = {{
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
    GUID_NAMETAB(CROS_KERNEL),
    GUID_NAMETAB(CROS_ROOTFS),
    GUID_NAMETAB(CROS_RESERVED),
    GUID_NAMETAB(CROS_FIRMWARE),
    GUID_NAMETAB(CROS_DATA),
    GUID_NAMETAB(BIOS),
    GUID_NAMETAB(EMMC_BOOT1),
    GUID_NAMETAB(EMMC_BOOT2),
    GUID_NAMETAB(LINUX_FILESYSTEM_DATA),
}};
// clang-format on

// Match keywords (like GUID_SYSTEM_NAME) and convert them to their
// corresponding byte sequences. 'out' should point to a GPT_GUID_LEN array.
// Return false if no match if found.
bool KnownGuid::NameToGuid(const char* name, uint8_t* out) {
  if (name == NULL) {
    return false;
  }

  for (auto const& guidp : nametab_) {
    if (!strcmp(name, guidp.name())) {
      memcpy(out, guidp.guid(), GPT_GUID_LEN);
      return true;
    }
  }

  return false;
}

const char* KnownGuid::GuidToName(const uint8_t* guid) {
  if (guid == NULL) {
    return nullptr;
  }

  for (auto const& guidp : nametab_) {
    if (memcmp(guidp.guid(), guid, sizeof(guid_t)) == 0) {
      return guidp.name();
    }
  }
  return nullptr;
}

const char* KnownGuid::GuidStrToName(const char* str) {
  if (str == NULL) {
    return nullptr;
  }

  for (auto const& guidp : nametab_) {
    if (strcmp(guidp.str(), str) == 0) {
      return guidp.name();
    }
  }
  return nullptr;
}

}  // namespace gpt
