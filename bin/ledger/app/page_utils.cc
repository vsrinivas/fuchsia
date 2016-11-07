// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_utils.h"

#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/mtl/shared_buffer/strings.h"

namespace ledger {
namespace {
Status ToBuffer(convert::ExtendedStringView value,
                int64_t offset,
                int64_t max_size,
                mojo::ScopedSharedBufferHandle* buffer) {
  size_t start = value.size();
  // Valid indices are between -N and N-1.
  if (offset >= -static_cast<int64_t>(value.size()) &&
      offset < static_cast<int64_t>(value.size())) {
    start = offset < 0 ? value.size() + offset : offset;
  }
  size_t length = max_size < 0 ? value.size() : max_size;

  bool result =
      mtl::SharedBufferFromString(value.substr(start, length), buffer);
  return result ? Status::OK : Status::UNKNOWN_ERROR;
}

void GetReferenceAsStringView(
    storage::PageStorage* storage,
    convert::ExtendedStringView opaque_id,
    std::function<void(Status, ftl::StringView)> callback) {
  storage->GetObject(
      opaque_id, [callback](storage::Status status,
                            std::unique_ptr<const storage::Object> object) {
        if (status != storage::Status::OK) {
          callback(
              PageUtils::ConvertStatus(status, Status::REFERENCE_NOT_FOUND),
              ftl::StringView());
          return;
        }
        ftl::StringView data;
        status = object->GetData(&data);
        if (status != storage::Status::OK) {
          callback(
              PageUtils::ConvertStatus(status, Status::REFERENCE_NOT_FOUND),
              ftl::StringView());
          return;
        }

        callback(Status::OK, data);
      });
}

}  // namespace

Status PageUtils::ConvertStatus(storage::Status status,
                                Status not_found_status) {
  switch (status) {
    case storage::Status::OK:
      return Status::OK;
    case storage::Status::IO_ERROR:
      return Status::IO_ERROR;
    case storage::Status::NOT_FOUND:
      return not_found_status;
    default:
      return Status::INTERNAL_ERROR;
  }
}

void PageUtils::GetReferenceAsValuePtr(
    storage::PageStorage* storage,
    convert::ExtendedStringView reference_id,
    std::function<void(Status, ValuePtr)> callback) {
  GetReferenceAsStringView(
      storage, reference_id, [callback](Status status, ftl::StringView data) {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        if (data.size() <= kMaxInlineDataSize) {
          ValuePtr value = Value::New();
          value->set_bytes(convert::ToArray(data));
          callback(Status::OK, std::move(value));
          return;
        }

        mojo::ScopedSharedBufferHandle buffer;
        Status buffer_status = ToBuffer(data, 0, -1, &buffer);
        if (buffer_status != Status::OK) {
          callback(buffer_status, nullptr);
          return;
        }
        ValuePtr value = Value::New();
        value->set_buffer(std::move(buffer));
        callback(Status::OK, std::move(value));
      });
}

void PageUtils::GetPartialReferenceAsBuffer(
    storage::PageStorage* storage,
    convert::ExtendedStringView reference_id,
    int64_t offset,
    int64_t max_size,
    std::function<void(Status, mojo::ScopedSharedBufferHandle)> callback) {
  GetReferenceAsStringView(
      storage, reference_id,
      [offset, max_size, callback](Status status, ftl::StringView data) {
        if (status != Status::OK) {
          callback(status, mojo::ScopedSharedBufferHandle());
          return;
        }
        mojo::ScopedSharedBufferHandle buffer;
        Status buffer_status = ToBuffer(data, offset, max_size, &buffer);
        if (buffer_status != Status::OK) {
          callback(buffer_status, mojo::ScopedSharedBufferHandle());
          return;
        }
        callback(Status::OK, std::move(buffer));
      });
}

}  // namespace ledger
