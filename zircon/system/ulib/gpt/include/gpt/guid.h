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
#include <list>
#include <optional>
#include <string>
#include <string_view>

#include <gpt/gpt.h>
#include <src/lib/uuid/uuid.h>

namespace gpt {

// Whether to use the new or legacy partition scheme.
enum class PartitionScheme { kNew, kLegacy };

// Number of GUID_.*[STRING|VALUE|NAME] pairs
constexpr uint8_t kKnownGuidEntries = 41;

class GuidProperties {
 public:
  GuidProperties(std::string_view name, uuid::Uuid type_guid, PartitionScheme scheme)
      : name_(name), type_guid_(std::move(type_guid)), scheme_(scheme) {}

  // The GPT partition name.
  std::string_view name() const { return name_; }

  // The GPT partition type GUID.
  const uuid::Uuid& type_guid() const { return type_guid_; }

  // Whether this entry belongs to the legacy or new partition scheme.
  PartitionScheme scheme() const { return scheme_; }

 private:
  std::string_view name_;
  uuid::Uuid type_guid_;
  PartitionScheme scheme_;
};

class KnownGuid {
 public:
  using NameTable = std::array<GuidProperties, kKnownGuidEntries>;

  // Returns all GuidProperties matching the given criteria.
  static std::list<const GuidProperties*> Find(std::optional<std::string_view> name,
                                               std::optional<uuid::Uuid> type_guid,
                                               std::optional<PartitionScheme> scheme);

  // Returns a string describing the given type GUID.
  //
  // If the type GUID uniquely identifies a partition, that partition name will
  // be returned. If the type GUID is shared between slotted partitions e.g.
  // zircon_{a,b,r}, then something like "zircon_*" will be returned.
  //
  // This string should only be used for logging and informational purposes,
  // it may not correspond to an actual name in the GPT.
  //
  // Returns empty string if no match was found.
  static std::string TypeDescription(const uuid::Uuid& type_guid);
  static std::string TypeDescription(const uint8_t* type_guid);

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
