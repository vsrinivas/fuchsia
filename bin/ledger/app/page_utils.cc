// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_utils.h"

#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fsl/vmo/strings.h"

namespace ledger {
namespace {
Status ToBuffer(convert::ExtendedStringView value,
                int64_t offset,
                int64_t max_size,
                zx::vmo* buffer) {
  size_t start = value.size();
  // Valid indices are between -N and N-1.
  if (offset >= -static_cast<int64_t>(value.size()) &&
      offset < static_cast<int64_t>(value.size())) {
    start = offset < 0 ? value.size() + offset : offset;
  }
  size_t length = max_size < 0 ? value.size() : max_size;

  bool result = fsl::VmoFromString(value.substr(start, length), buffer);
  return result ? Status::OK : Status::UNKNOWN_ERROR;
}

}  // namespace

void PageUtils::GetReferenceAsStringView(
    storage::PageStorage* storage,
    convert::ExtendedStringView opaque_id,
    storage::PageStorage::Location location,
    Status not_found_status,
    std::function<void(Status, fxl::StringView)> callback) {
  storage->GetObject(
      opaque_id, location,
      [not_found_status, callback](
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
    default:
      FXL_DCHECK(false) << "Internal error in Ledger storage. Status: "
                        << status;
      return Status::INTERNAL_ERROR;
  }
}

void PageUtils::GetPartialReferenceAsBuffer(
    storage::PageStorage* storage,
    convert::ExtendedStringView reference_id,
    int64_t offset,
    int64_t max_size,
    storage::PageStorage::Location location,
    Status not_found_status,
    std::function<void(Status, zx::vmo)> callback) {
  GetReferenceAsStringView(
      storage, reference_id, location, not_found_status,
      [offset, max_size, callback](Status status, fxl::StringView data) {
        if (status != Status::OK) {
          callback(status, zx::vmo());
          return;
        }
        zx::vmo buffer;
        Status buffer_status = ToBuffer(data, offset, max_size, &buffer);
        if (buffer_status != Status::OK) {
          callback(buffer_status, zx::vmo());
          return;
        }
        callback(Status::OK, std::move(buffer));
      });
}

bool PageUtils::MatchesPrefix(const std::string& key,
                              const std::string& prefix) {
  return convert::ExtendedStringView(key).substr(0, prefix.size()) ==
         convert::ExtendedStringView(prefix);
}

}  // namespace ledger
