// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/inlined_object_impl.h"

namespace storage {

InlinedObjectImpl::InlinedObjectImpl(ObjectId id) : id_(id) {}

InlinedObjectImpl::~InlinedObjectImpl() {}

ObjectId InlinedObjectImpl::GetId() const {
  return id_;
}

Status InlinedObjectImpl::GetData(ftl::StringView* data) const {
  *data = id_;
  return Status::OK;
}

}  // namespace storage
