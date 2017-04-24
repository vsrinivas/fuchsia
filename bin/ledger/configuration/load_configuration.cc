// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/load_configuration.h"

#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"

namespace configuration {

bool LoadConfiguration(Configuration* result) {
  const std::string config_file = kDefaultConfigurationFile.ToString();

  // Get the current configuration.
  Configuration config;
  if (files::IsFile(config_file)) {
    if (!ConfigurationEncoder::Decode(config_file, &config)) {
      FTL_LOG(ERROR) << "The configuration file is present at: "
                     << kDefaultConfigurationFile << " but can't be read.";
      return false;
    }

    FTL_LOG(INFO) << "Read the configuration file at "
                  << kDefaultConfigurationFile;
  } else {
    FTL_LOG(WARNING)
        << "No Ledger configuration - will work locally but won't sync. "
        << "To enable cloud sync, see "
        << "https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md";
  }

  *result = config;
  return true;
}

}  // namespace configuration
