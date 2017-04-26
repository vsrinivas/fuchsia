// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/clean_command.h"

#include <string>

#include "apps/ledger/src/cloud_sync/impl/paths.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"

namespace tool {

CleanCommand::CleanCommand(const cloud_sync::UserConfig& user_config,
                           ftl::StringView user_repository_path,
                           ledger::NetworkService* network_service)
    : user_repository_path_(user_repository_path.ToString()) {
  FTL_DCHECK(!user_repository_path_.empty());
  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service, user_config.server_id,
      cloud_sync::GetFirebasePathForUser(user_config.cloud_prefix,
                                         user_config.user_id));
}

void CleanCommand::Start(ftl::Closure on_done) {
  std::cout << "> Deleting " << user_repository_path_ << " ";
  if (!files::DeletePath(user_repository_path_, true)) {
    std::cout << std::endl;
    FTL_LOG(ERROR) << "Unable to delete user local storage at "
                   << user_repository_path_;
    on_done();
    return;
  }
  std::cout << "OK" << std::endl;

  std::cout << "> Erasing " << firebase_->api_url() << " ";
  firebase_->Delete(
      "", [on_done = std::move(on_done)](firebase::Status status) {
        std::cout << status << std::endl;
        on_done();
      });
}

}  // namespace tool
