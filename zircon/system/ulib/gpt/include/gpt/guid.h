// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPT_GUID_H_
#define GPT_GUID_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <array>

#include <gpt/gpt.h>

namespace gpt {

// Since the human-readable representation of a GUID is the following format,
// ordered little-endian, it is useful to group a GUID into these
// appropriately-sized groups.
struct guid_t {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};
static_assert(sizeof(guid_t) == GPT_GUID_LEN, "unexpected guid_t size");

// Number of GUID_.*[STRING|VALUE|NAME] pairs
constexpr uint8_t kKnownGuidEntries = 25;

class GuidProperties {
 public:
  GuidProperties(const char* name, const char* str, const uint8_t guid[GPT_GUID_LEN]) {
    ZX_ASSERT(strlen(name) < sizeof(name_));
    ZX_ASSERT(strlen(str) == sizeof(str_) - 1);
    strncpy(name_, name, sizeof(name_));
    strncpy(str_, str, sizeof(str_) - 1);
    memcpy(guid_, guid, GPT_GUID_LEN);
  }

  const char* name() const { return name_; }

  const char* str() const { return str_; }

  const uint8_t* guid() const { return guid_; }

 private:
  char name_[(GPT_NAME_LEN / 2) + 1];
  char str_[kGuidStrLength];
  uint8_t guid_[GPT_GUID_LEN];
};

class KnownGuid {
 public:
  using NameTable = std::array<GuidProperties, kKnownGuidEntries>;

  // Given a known guid name, like "fuchsia-blob", converts it
  // it's guid_t equivalent. On failure, returns false.
  static bool NameToGuid(const char* name, uint8_t* out);

  // Converts a known guid_t value to it's name.
  // If there exists no name for the guid, returns nullptr.
  static const char* GuidToName(const uint8_t* guid);

  // Converts a known guid str, like "2967380E-134C-4CBB-B6DA-17E7CE1CA45D",
  // to a name "fuchsia-blob".
  // If there exists no name for the guid, returns nullptr.
  static const char* GuidStrToName(const char* str);

  using const_iterator = NameTable::const_iterator;

  static const_iterator begin() { return nametab_.begin(); }
  static const_iterator end() { return nametab_.end(); }
  static const_iterator cbegin() { return nametab_.cbegin(); }
  static const_iterator cend() { return nametab_.cend(); }

 private:
  static const NameTable nametab_;
};

}  // namespace gpt

#endif  // GPT_GUID_H_
