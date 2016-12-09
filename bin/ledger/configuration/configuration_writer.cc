// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_view.h"

namespace {
const char kHelpArg[] = "help";
const char kConfigPathArg[] = "config_path";
const char kFirebaseIdArg[] = "firebase_id";
const char kFirebasePrefixArg[] = "firebase_prefix";
const char kSyncArg[] = "sync";
const char kNoSyncArg[] = "nosync";

void PrintHelp() {
  printf("Creates the configuration file used by Ledger.\n");
  printf("\n");
  printf("Optional, global arguments:\n");
  printf("  --config_path=/path/to/config/file: path to the configuration \n");
  printf("    file to write to (default: /data/ledger/config.json).\n");
  printf("  --help: prints this help.\n");
  printf("Cloud Sync configuration:\n");
  printf("  (passing either implies --sync unless --nosync is passed)\n");
  printf("  --firebase_id=<NAME_OF_FIREBASE_INSTANCE>\n");
  printf("  --firebase_prefix=<USER_SPECIFIC_PREFIX>\n");
  printf("Toggle Cloud Sync off and on:\n");
  printf("  --sync\n");
  printf("  --nosync\n");
}
}

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption(kHelpArg)) {
    PrintHelp();
    return 0;
  }

  std::string config_path = configuration::kDefaultConfigurationFile.ToString();
  if (command_line.HasOption(kConfigPathArg)) {
    bool ret = command_line.GetOptionValue(kConfigPathArg, &config_path);
    FTL_DCHECK(ret);
  }
  if (config_path.empty()) {
    FTL_LOG(ERROR) << "Specify a non-empty " << kConfigPathArg;
    return 1;
  }

  configuration::Configuration config;
  if (files::IsFile(config_path) &&
      !configuration::ConfigurationEncoder::Decode(config_path, &config)) {
    FTL_LOG(WARNING) << "Found existing configuration file at: " << config_path
                     << ", but failed to decode it. "
                     << "Starting from the default configuration.";
    config = configuration::Configuration();
  }

  if (command_line.HasOption(kFirebaseIdArg)) {
    config.use_sync = true;
    bool ret = command_line.GetOptionValue(kFirebaseIdArg,
                                           &config.sync_params.firebase_id);
    FTL_DCHECK(ret);
  }

  if (command_line.HasOption(kFirebasePrefixArg)) {
    config.use_sync = true;
    bool ret = command_line.GetOptionValue(kFirebasePrefixArg,
                                           &config.sync_params.firebase_prefix);
    FTL_DCHECK(ret);
  }

  if (command_line.HasOption(kSyncArg) && command_line.HasOption(kNoSyncArg)) {
    FTL_LOG(ERROR)
        << "Ledger isn't a Schroedinger notepad, it either syncs or not";
    return 1;
  }

  if (command_line.HasOption(kSyncArg)) {
    config.use_sync = true;
  }

  if (command_line.HasOption(kNoSyncArg)) {
    config.use_sync = false;
  }

  if (config.use_sync && (config.sync_params.firebase_id.empty() ||
                          config.sync_params.firebase_prefix.empty())) {
    FTL_LOG(ERROR) << "To enable Cloud Sync pass both --firebase_id "
                   << "and --firebase_prefix";
    return 1;
  }

  if (!files::CreateDirectory(files::GetDirectoryName(config_path))) {
    FTL_LOG(ERROR) << "Unable to create directory for file " << config_path;
  }
  if (!configuration::ConfigurationEncoder::Write(config_path, config)) {
    FTL_LOG(ERROR) << "Unable to write to file " << config_path;
    return 1;
  }

  return 0;
}
