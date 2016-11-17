// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/environment/environment.h"

namespace ledger {

Environment::Environment(configuration::Configuration configuration,
                         NetworkService* network_service)
    : configuration(std::move(configuration)),
      network_service(network_service) {}

Environment::~Environment() {}

}  // namespace ledger
