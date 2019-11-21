// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_utils.h"

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

void PageUtils::ResolveObjectIdentifierAsStringView(
    storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
    storage::PageStorage::Location location,
    fit::function<void(Status, absl::string_view)> callback) {
  storage->GetObject(std::move(object_identifier), location,
                     [callback = std::move(callback)](
                         Status status, std::unique_ptr<const storage::Object> object) {
                       if (status != Status::OK) {
                         callback(status, absl::string_view());
                         return;
                       }
                       absl::string_view data;
                       status = object->GetData(&data);
                       if (status != Status::OK) {
                         callback(status, absl::string_view());
                         return;
                       }

                       callback(Status::OK, data);
                     });
}

bool PageUtils::MatchesPrefix(const std::string& key, const std::string& prefix) {
  return convert::ExtendedStringView(key).substr(0, prefix.size()) ==
         convert::ExtendedStringView(prefix);
}

}  // namespace ledger
