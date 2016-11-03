// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider/public/record.h"

namespace cloud_provider {

Record::Record() = default;

Record::Record(Notification&& n, std::string&& t)
    : notification(std::move(n)), timestamp(std::move(t)) {}

Record::~Record() = default;

Record::Record(Record&&) = default;

Record& Record::operator=(Record&&) = default;

}  // namespace cloud_provider
