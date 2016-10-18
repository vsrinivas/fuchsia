// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/public/types.h"

namespace storage {

bool operator==(const Entry& lhs, const Entry& rhs) {
  return lhs.key == rhs.key && lhs.object_id == rhs.object_id &&
         lhs.priority == rhs.priority;
}

bool operator!=(const Entry& lhs, const Entry& rhs) {
  return !(lhs == rhs);
}

}  // namespace
