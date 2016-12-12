// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/configuration.h"

#include <string>

namespace configuration {

Configuration::Configuration() : use_sync(false) {}

Configuration::Configuration(const Configuration&) = default;

Configuration::Configuration(Configuration&&) = default;

Configuration& Configuration::operator=(const Configuration&) = default;

Configuration& Configuration::operator=(Configuration&&) = default;

bool operator==(const Configuration& lhs, const Configuration& rhs) {
  return lhs.use_sync == rhs.use_sync && lhs.sync_params == rhs.sync_params;
}

bool operator!=(const Configuration& lhs, const Configuration& rhs) {
  return !(lhs == rhs);
}

bool operator==(const Configuration::SyncParams& lhs,
                const Configuration::SyncParams& rhs) {
  return lhs.firebase_id == rhs.firebase_id &&
         lhs.firebase_prefix == rhs.firebase_prefix;
}

bool operator!=(const Configuration::SyncParams& lhs,
                const Configuration::SyncParams& rhs) {
  return !(lhs == rhs);
}

}  // namespace configuration
