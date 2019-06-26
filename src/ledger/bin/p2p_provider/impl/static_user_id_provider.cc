// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"

namespace p2p_provider {

StaticUserIdProvider::StaticUserIdProvider(std::string user_id) : user_id_(user_id) {}

StaticUserIdProvider::~StaticUserIdProvider() = default;

void StaticUserIdProvider::GetUserId(fit::function<void(Status, std::string)> callback) {
  callback(Status::OK, user_id_);
}

}  // namespace p2p_provider
