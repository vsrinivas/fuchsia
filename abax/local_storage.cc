// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/local_storage.h"

#include <string>

#include "apps/ledger/convert/convert.h"
#include "lib/ftl/logging.h"

namespace ledger {

LocalStorage::LocalStorage(std::map<std::string, std::string>* db,
                           Serialization* serialization)
    : db_(db), serialization_(serialization) {}

LocalStorage::~LocalStorage() {}

bool LocalStorage::WriteEntryValue(convert::ExtendedStringView entry_value,
                                   std::string* value_row_key) {
  *value_row_key = serialization_->GetValueRowKey(entry_value);
  (*db_)[*value_row_key] = convert::ToString(entry_value);
  return true;
}

bool LocalStorage::WriteReference(const mojo::Array<uint8_t>& entry_key,
                                  convert::ExtendedStringView value_row_key) {
  (*db_)[serialization_->GetReferenceRowKey(entry_key)] =
      convert::ToString(value_row_key);
  return true;
}

}  // namespace ledger
