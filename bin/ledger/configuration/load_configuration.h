// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CONFIGURATION_LOAD_CONFIGURATION_H_
#define APPS_LEDGER_SRC_CONFIGURATION_LOAD_CONFIGURATION_H_

#include "apps/ledger/src/configuration/configuration.h"

namespace configuration {

// Loads Ledger configuration from the default location, and validates it for
// compatibility with the last run configuration. Returns true if the the
// configuration is valid and stored in |result| and false otherwise.
bool LoadConfiguration(Configuration* result);

// Saves the given configuration at the last one used. This configuration will
// be used for compatibility check the next time LoadConfiguration() is called..
bool SaveAsLastConfiguration(const Configuration& configuration);

}  // namespace configuration

#endif  // APPS_LEDGER_SRC_CONFIGURATION_LOAD_CONFIGURATION_H_
