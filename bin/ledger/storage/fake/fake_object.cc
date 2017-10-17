// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_object.h"

namespace storage {
namespace fake {

FakeObject::FakeObject(ObjectDigestView digest, fxl::StringView content)
    : digest_(digest.ToString()), content_(content.ToString()) {}

FakeObject::~FakeObject() {}

ObjectDigest FakeObject::GetDigest() const {
  return digest_;
}

Status FakeObject::GetData(fxl::StringView* data) const {
  *data = content_;
  return Status::OK;
}

}  // namespace fake
}  // namespace storage
