// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CONFIG_H_
#define SRC_STORAGE_FSHOST_CONFIG_H_

#include "src/storage/fshost/fshost-boot-args.h"
#include "src/storage/fshost/fshost_config.h"

namespace fshost {

// Returns the default/base configuration for fshost when run without configuration from the
// component framework.
fshost_config::Config DefaultConfig();

// Returns an all-false/all-zeroes/empty-strings config. Used in tests to override specific settings
// while testing the default behavior elsewhere.
fshost_config::Config EmptyConfig();

// Read boot arguments and apply any fshost-related options to our configuration.
void ApplyBootArgsToConfig(fshost_config::Config& config, const FshostBootArgs& boot_args);

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_CONFIG_H_
