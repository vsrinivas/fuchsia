// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_
#define APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_

#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/network/network_service.h"

namespace ledger {

// Environment for the ledger application.
struct Environment {
  Environment(configuration::Configuration configuration,
              NetworkService* network_service);
  ~Environment();

  const configuration::Configuration configuration;
  NetworkService* const network_service;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_
