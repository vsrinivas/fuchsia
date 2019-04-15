// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_utils.h"

#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>

#include <memory>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

void PageUtils::ResolveObjectIdentifierAsStringView(
    storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
    storage::PageStorage::Location location,
    fit::function<void(storage::Status, fxl::StringView)> callback) {
  storage->GetObject(std::move(object_identifier), location,
                     [callback = std::move(callback)](
                         storage::Status status,
                         std::unique_ptr<const storage::Object> object) {
                       if (status != storage::Status::OK) {
                         callback(status, fxl::StringView());
                         return;
                       }
                       fxl::StringView data;
                       status = object->GetData(&data);
                       if (status != storage::Status::OK) {
                         callback(status, fxl::StringView());
                         return;
                       }

                       callback(storage::Status::OK, data);
                     });
}

Status PageUtils::ConvertStatus(storage::Status status) {
  switch (status) {
    case storage::Status::OK:
      return Status::OK;
    case storage::Status::IO_ERROR:
      return Status::IO_ERROR;
    case storage::Status::PAGE_NOT_FOUND:
      return Status::PAGE_NOT_FOUND;
    case storage::Status::KEY_NOT_FOUND:
      return Status::KEY_NOT_FOUND;
    case storage::Status::REFERENCE_NOT_FOUND:
      return Status::REFERENCE_NOT_FOUND;
    case storage::Status::NETWORK_ERROR:
      return Status::NETWORK_ERROR;
    case storage::Status::INTERRUPTED:
      FXL_LOG(WARNING) << "Interrupted status must be handled internally.";
      return Status::INTERNAL_ERROR;
    default:
      FXL_DCHECK(false) << "Internal error in Ledger storage. storage::Status: "
                        << status;
      return Status::INTERNAL_ERROR;
  }
}

void PageUtils::ResolveObjectIdentifierAsBuffer(
    storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
    int64_t offset, int64_t max_size, storage::PageStorage::Location location,
    fit::function<void(storage::Status, fsl::SizedVmo)> callback) {
  storage->GetObjectPart(
      object_identifier, offset, max_size, location,
      [callback = std::move(callback)](storage::Status status,
                                       fsl::SizedVmo object_part) {
        callback(status, std::move(object_part));
      });
}

bool PageUtils::MatchesPrefix(const std::string& key,
                              const std::string& prefix) {
  return convert::ExtendedStringView(key).substr(0, prefix.size()) ==
         convert::ExtendedStringView(prefix);
}

}  // namespace ledger
