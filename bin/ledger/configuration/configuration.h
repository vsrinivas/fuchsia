// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_H_
#define APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_H_

#include <string>

#include "lib/ftl/strings/string_view.h"

namespace configuration {

constexpr ftl::StringView kDefaultConfigurationFile =
    "/data/ledger/config.json";

// The configuration for the Ledger.
struct Configuration {
  // Creates a default, empty configuration.
  Configuration();
  Configuration(const Configuration&);
  Configuration(Configuration&&);

  Configuration& operator=(const Configuration&);
  Configuration& operator=(Configuration&&);

  // Set to true to enable Cloud Sync. False by default.
  bool use_sync;

  // Cloud Sync parameters.
  struct SyncParams {
    // ID of the firebase instance.
    std::string firebase_id;
    // Prefix of firebase keys.
    std::string firebase_prefix;
  };

  // sync_params holds the parameters used for cloud synchronization if
  // |use_sync| is true.
  SyncParams sync_params;
};

bool operator==(const Configuration& lhs, const Configuration& rhs);
bool operator==(const Configuration::SyncParams& lhs,
                const Configuration::SyncParams& rhs);
}  // namespace configuration

#endif  // APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_H_
