// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_view.h"

namespace {
constexpr ftl::StringView kHelpArg = "--help";
constexpr ftl::StringView kHelpShortArg = "-h";
constexpr ftl::StringView kConfigPathArg = "--config_path=";
constexpr ftl::StringView kFirebaseIdArg = "--firebase_id=";
constexpr ftl::StringView kFirebasePrefixArg = "--firebase_prefix=";

bool IsArgument(const std::string& arg, ftl::StringView argument_name) {
  return arg.size() >= argument_name.size() &&
         arg.substr(0, argument_name.size()) == argument_name;
}

void PrintHelp() {
  printf("Creates the configuration file used by the Ledger.\n");
  printf("\n");
  printf("Optional, global arguments:\n");
  printf("  --config_path=/path/to/config/file: path to the configuration \n");
  printf("    file to write to (default: /data/ledger/config.json).\n");
  printf("  --help: prints this help.\n");
  printf("Synchronization arguments (these enable cloud sync):\n");
  printf("  --firebase_id=<NAME_OF_FIREBASE_INSTANCE>\n");
  printf("  --firebase_prefix=<USER_SPECIFIC_PREFIX>\n");
}
}

int main(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);

  configuration::Configuration config;
  ftl::StringView config_path = configuration::kDefaultConfigurationFile;
  bool first_skipped = false;
  for (const std::string& arg : args) {
    if (!first_skipped) {
      // Skip arg[0].
      first_skipped = true;
    } else if (IsArgument(arg, kHelpArg) || IsArgument(arg, kHelpShortArg)) {
      PrintHelp();
      return 0;
    } else if (IsArgument(arg, kConfigPathArg)) {
      config_path = ftl::StringView(arg).substr(kConfigPathArg.size());
    } else if (IsArgument(arg, kFirebaseIdArg)) {
      config.use_sync = true;
      config.sync_params.firebase_id = arg.substr(kFirebaseIdArg.size());
    } else if (IsArgument(arg, kFirebasePrefixArg)) {
      config.use_sync = true;
      config.sync_params.firebase_prefix =
          arg.substr(kFirebasePrefixArg.size());
    } else {
      FTL_LOG(ERROR) << "Unrecognized argument " << arg;
      PrintHelp();
      return 1;
    }
  }

  if (config.use_sync && (config.sync_params.firebase_id.empty() ||
                          config.sync_params.firebase_prefix.empty())) {
    FTL_LOG(ERROR) << "Please specify both --firebase_id and --firebase_prefix";
    return 1;
  }

  if (config_path.empty()) {
    FTL_LOG(ERROR) << "Please specify a non-empty directory path to write to.";
    return 1;
  }

  std::string config_path_string = config_path.ToString();
  if (!files::CreateDirectory(files::GetDirectoryName(config_path_string))) {
    FTL_LOG(ERROR) << "Unable to create directory for file " << config_path;
  }
  if (!configuration::ConfigurationEncoder::Write(config_path_string, config)) {
    FTL_LOG(ERROR) << "Unable to write to file " << config_path;
    return 1;
  }

  return 0;
}
