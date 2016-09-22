// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider/public/record.h"

namespace cloud_provider {

Record::Record(const Notification& n, std::string t)
    : notification(n), timestamp(t) {}

}  // namespace cloud_provider
