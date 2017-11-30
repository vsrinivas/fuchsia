// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/make_object_identifier.h"

#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {

ObjectIdentifier MakeDefaultObjectIdentifier(ObjectDigest digest) {
  return {kDefaultKeyIndex, kDefaultDeletionScopeId, std::move(digest)};
}

}  // namespace storage
