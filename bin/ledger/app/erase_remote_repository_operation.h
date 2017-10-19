// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
#define PERIDOT_BIN_LEDGER_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_

#include <memory>
#include <string>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/firebase/firebase_impl.h"

namespace ledger {

// Asynchronous operation that erases data in the cloud for the given
// repository.
class EraseRemoteRepositoryOperation {
 public:
  EraseRemoteRepositoryOperation(
      fxl::RefPtr<fxl::TaskRunner> task_runner,
      cloud_provider::CloudProviderPtr cloud_provider);
  ~EraseRemoteRepositoryOperation();

  EraseRemoteRepositoryOperation(EraseRemoteRepositoryOperation&& other);
  EraseRemoteRepositoryOperation& operator=(
      EraseRemoteRepositoryOperation&& other);

  void Start(std::function<void(bool)> on_done);

 private:
  void ClearDeviceMap();

  void EraseRemote();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  cloud_provider::CloudProviderPtr cloud_provider_;
  cloud_provider::DeviceSetPtr device_set_;

  std::function<void(bool)> on_done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EraseRemoteRepositoryOperation);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
