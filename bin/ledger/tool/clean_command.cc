// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/clean_command.h"

#include <string>

#include "apps/ledger/src/cloud_sync/impl/paths.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"

namespace tool {

CleanCommand::CleanCommand(const cloud_sync::UserConfig& user_config,
                           ftl::StringView user_repository_path,
                           ledger::NetworkService* network_service,
                           bool force)
    : user_repository_path_(user_repository_path.ToString()), force_(force) {
  FTL_DCHECK(!user_repository_path_.empty());
  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service, user_config.server_id,
      cloud_sync::GetFirebasePathForUser(user_config.user_id));
}

void CleanCommand::Start(ftl::Closure on_done) {
  if (!force_) {
    std::cout << std::endl;
    std::cout << "About to delete: " << std::endl;
    std::cout << " - local data at " << user_repository_path_ << std::endl;
    std::cout << " - remote data at " << firebase_->api_url() << std::endl;
    std::cout << "Sounds good? (enter \"yes\" to confirm)" << std::endl;
    std::string answer;
    std::getline(std::cin, answer);
    if (answer != "yes") {
      std::cout << "As you prefer, bye." << std::endl;
      on_done();
      return;
    }
  }

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
      "", {}, [on_done = std::move(on_done)](firebase::Status status) {
        std::cout << status << std::endl;
        on_done();
      });
}

}  // namespace tool
