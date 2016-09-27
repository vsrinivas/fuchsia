// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/object_impl.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <vector>

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"

namespace storage {

ObjectImpl::ObjectImpl(const ObjectId& id, const std::string& file_path)
    : id_(id), file_path_(file_path) {}

ObjectImpl::~ObjectImpl() {}

ObjectId ObjectImpl::GetId() const {
  return id_;
}

Status ObjectImpl::GetSize(int64_t* size) {
  if (data_.empty()) {
    int64_t res;
    if (!files::GetFileSize(file_path_, &res)) {
      return Status::IO_ERROR;
    }
    *size = res;
    return Status::OK;
  }
  *size = data_.size();
  return Status::OK;
}

Status ObjectImpl::GetData(const uint8_t** data) {
  if (data_.empty()) {
    std::string res;
    // TODO(nellyv): Replace with mmap when supported.
    if (!files::ReadFileToString(file_path_.data(), &res)) {
      return Status::IO_ERROR;
    }
    data_.swap(res);
  }
  *data = reinterpret_cast<const uint8_t*>(data_.data());
  return Status::OK;
}

}  // namespace storage
