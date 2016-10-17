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

ObjectImpl::ObjectImpl(ObjectId&& id, std::string&& file_path)
    : id_(id), file_path_(file_path) {}

ObjectImpl::~ObjectImpl() {}

ObjectId ObjectImpl::GetId() const {
  return id_;
}

Status ObjectImpl::GetData(ftl::StringView* data) {
  if (data_.empty()) {
    std::string res;
    // TODO(nellyv): Replace with mmap when supported.
    if (!files::ReadFileToString(file_path_.data(), &res)) {
      return Status::INTERNAL_IO_ERROR;
    }
    data_.swap(res);
  }
  *data = data_;
  return Status::OK;
}

}  // namespace storage
