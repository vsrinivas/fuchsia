// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/public/constants.h"

#include "apps/ledger/src/convert/convert.h"

namespace storage {

static_assert(sizeof(convert::IdStorage) == kCommitIdSize,
              "storage size for id is incorrect");

}  // namespace storage
