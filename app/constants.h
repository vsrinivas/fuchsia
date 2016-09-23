// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_CONSTANTS_H_
#define APPS_LEDGER_APP_CONSTANTS_H_

#include "apps/ledger/storage/public/types.h"

namespace ledger {

// The size of a page id array.
const size_t kPageIdSize = 16;

// The root id. The array size must be equal to kPageIdSize.
extern const char kRootPageId[kPageIdSize];

}  // namespace ledger

#endif  // APPS_LEDGER_APP_CONSTANTS_H_
