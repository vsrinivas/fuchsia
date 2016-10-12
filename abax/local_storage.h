// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_LOCAL_STORAGE_H_
#define APPS_LEDGER_ABAX_LOCAL_STORAGE_H_

#include <map>

#include "apps/ledger/abax/serialization.h"

namespace ledger {

class LocalStorage {
 public:
  LocalStorage(std::map<std::string, std::string>* db,
               Serialization* serialization);
  ~LocalStorage();

  // Writes the given entry value. The row under which the value is
  // written is decided based on its content, and written to |value_row_key|.
  bool WriteEntryValue(convert::ExtendedStringView entry_value,
                       std::string* value_row_key);

  // Writes the reference for the given entry key.
  bool WriteReference(const mojo::Array<uint8_t>& entry_key,
                      convert::ExtendedStringView value_row_key);

 private:
  std::map<std::string, std::string>* const db_;

  Serialization* serialization_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_LOCAL_STORAGE_H_
