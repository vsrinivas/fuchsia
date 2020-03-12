// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/identifier_base.h"

#include <map>

#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

struct SpecialIdentifierRecord {
  SpecialIdentifier special;
  std::string_view name;  // Including the leading "$".
  bool has_data;
};

// Must be in order of enum value.
// clang-format off
const SpecialIdentifierRecord kRecords[] = {
  { SpecialIdentifier::kNone,     "",      false },
  { SpecialIdentifier::kEscaped,  "$",     true },
  { SpecialIdentifier::kAnon,     "$anon", false },
  { SpecialIdentifier::kMain,     "$main", false },
  { SpecialIdentifier::kPlt,      "$plt",  true },
  { SpecialIdentifier::kRegister, "$reg",  true },
};
// clang-format on

// Returns null if the name is not matched.
const SpecialIdentifierRecord* NameToRecord(std::string_view name) {
  static std::map<std::string_view, const SpecialIdentifierRecord*> lookup;
  if (lookup.empty()) {
    for (const auto& record : kRecords)
      lookup[record.name] = &record;
  }

  auto found = lookup.find(name);
  if (found == lookup.end())
    return nullptr;
  return found->second;
}

// Returns null if the name is not matched.
const SpecialIdentifierRecord* EnumToRecord(SpecialIdentifier si) {
  int index = static_cast<int>(si);
  if (index < 0 || index >= static_cast<int>(SpecialIdentifier::kLast))
    return nullptr;

  return &kRecords[index];
}

}  // namespace

std::string_view SpecialIdentifierToString(SpecialIdentifier si) {
  const SpecialIdentifierRecord* record = EnumToRecord(si);
  if (!record)
    return "";
  return record->name;
}

SpecialIdentifier StringToSpecialIdentifier(std::string_view name) {
  const SpecialIdentifierRecord* record = NameToRecord(name);
  if (!record)
    return SpecialIdentifier::kNone;
  return record->special;
}

bool SpecialIdentifierHasData(SpecialIdentifier si) {
  const SpecialIdentifierRecord* record = EnumToRecord(si);
  if (!record)
    return false;
  return record->has_data;
}

const char kAnonIdentifierComponentName[] = "$anon";

}  // namespace zxdb
