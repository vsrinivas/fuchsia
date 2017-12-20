// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_object.h"

namespace storage {
namespace fake {

FakeObject::FakeObject(ObjectIdentifier identifier, fxl::StringView content)
    : identifier_(std::move(identifier)), content_(content.ToString()) {}

FakeObject::~FakeObject() {}

ObjectIdentifier FakeObject::GetIdentifier() const {
  return identifier_;
}

Status FakeObject::GetData(fxl::StringView* data) const {
  *data = content_;
  return Status::OK;
}

}  // namespace fake
}  // namespace storage
