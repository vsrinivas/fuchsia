// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/client/clean_command.h"

#include <string>

#include "apps/ledger/src/cloud_sync/impl/paths.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"

namespace cloud_sync {

namespace {

const char kDefaultLedgerPath[] = "/data/ledger";

}  // namespace

CleanCommand::CleanCommand(const configuration::Configuration& configuration,
                           ledger::NetworkService* network_service)
    : configuration_(configuration),
      firebase_(std::make_unique<firebase::FirebaseImpl>(
          network_service,
          configuration_.sync_params.firebase_id,
          GetFirebasePathForLedger(configuration_.sync_params.cloud_prefix))) {}

void CleanCommand::Start(ftl::Closure on_done) {
  std::cout << "> Deleting " << kDefaultLedgerPath << std::endl;
  if (!files::DeletePath(kDefaultLedgerPath, true)) {
    FTL_LOG(ERROR) << "Unable to delete local storage at "
                   << kDefaultLedgerPath;
    on_done();
    return;
  }
  std::string config_path = configuration::kDefaultConfigurationFile.ToString();

  std::cout << "> Recreating " << config_path << std::endl;

  if (!files::CreateDirectory(files::GetDirectoryName(config_path))) {
    FTL_LOG(ERROR) << "Unable to create directory for file " << config_path;
    on_done();
    return;
  }

  if (!configuration::ConfigurationEncoder::Write(config_path,
                                                  configuration_)) {
    FTL_LOG(ERROR) << "Unable to write to file " << config_path;
    on_done();
    return;
  }

  std::cout << "> Erasing remote storage (firebase only): ";
  firebase_->Delete(
      "", [on_done = std::move(on_done)](firebase::Status status) {
        std::cout << status << std::endl;
        on_done();
      });
}

}  // namespace cloud_sync
