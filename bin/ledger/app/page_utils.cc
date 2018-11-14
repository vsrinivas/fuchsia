// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_utils.h"

#include <memory>

#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

void PageUtils::ResolveObjectIdentifierAsStringView(
    storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
    storage::PageStorage::Location location, Status not_found_status,
    fit::function<void(Status, fxl::StringView)> callback) {
  storage->GetObject(
      object_identifier, location,
      [not_found_status, callback = std::move(callback)](
          storage::Status status,
          std::unique_ptr<const storage::Object> object) {
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, not_found_status),
                   fxl::StringView());
          return;
        }
        fxl::StringView data;
        status = object->GetData(&data);
        if (status != storage::Status::OK) {
          callback(PageUtils::ConvertStatus(status, not_found_status),
                   fxl::StringView());
          return;
        }

        callback(Status::OK, data);
      });
}

Status PageUtils::ConvertStatus(storage::Status status,
                                Status not_found_status) {
  switch (status) {
    case storage::Status::OK:
      return Status::OK;
    case storage::Status::IO_ERROR:
      return Status::IO_ERROR;
    case storage::Status::NOT_FOUND:
      FXL_DCHECK(not_found_status != Status::INTERNAL_ERROR);
      return not_found_status;
    case storage::Status::NOT_CONNECTED_ERROR:
      return Status::NETWORK_ERROR;
    case storage::Status::INTERRUPTED:
      return Status::INTERNAL_ERROR;
    default:
      FXL_DCHECK(false) << "Internal error in Ledger storage. Status: "
                        << status;
      return Status::INTERNAL_ERROR;
  }
}

void PageUtils::ResolveObjectIdentifierAsBuffer(
    storage::PageStorage* storage, storage::ObjectIdentifier object_identifier,
    int64_t offset, int64_t max_size, storage::PageStorage::Location location,
    Status not_found_status,
    fit::function<void(Status, fsl::SizedVmo)> callback) {
  storage->GetObjectPart(
      object_identifier, offset, max_size, location,
      [not_found_status, callback = std::move(callback)](
          storage::Status status, fsl::SizedVmo object_part) {
        callback(ConvertStatus(status, not_found_status),
                 std::move(object_part));
      });
}

bool PageUtils::MatchesPrefix(const std::string& key,
                              const std::string& prefix) {
  return convert::ExtendedStringView(key).substr(0, prefix.size()) ==
         convert::ExtendedStringView(prefix);
}

}  // namespace ledger
