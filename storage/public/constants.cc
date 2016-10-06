// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/public/constants.h"

namespace storage {

const unsigned long kCommitIdSize = 32;
const unsigned long kObjectIdSize = 32;
const char kFirstPageCommitId[kCommitIdSize] = {0};

}  // namespace storage
