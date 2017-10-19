// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/erase_remote_repository_operation.h"

#include <utility>

#include "peridot/bin/ledger/auth_provider/auth_provider_impl.h"
#include "peridot/bin/ledger/backoff/exponential_backoff.h"
#include "peridot/bin/ledger/cloud_provider/impl/paths.h"
#include "peridot/bin/ledger/device_set/cloud_device_set_impl.h"

namespace ledger {

EraseRemoteRepositoryOperation::EraseRemoteRepositoryOperation(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    cloud_provider::CloudProviderPtr cloud_provider)
    : task_runner_(task_runner), cloud_provider_(std::move(cloud_provider)) {
  cloud_provider_.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the cloud provider "
                   << "while trying to erase the repository.";
    FXL_DCHECK(on_done_);
    on_done_(false);
  });
}

EraseRemoteRepositoryOperation::~EraseRemoteRepositoryOperation() {}

EraseRemoteRepositoryOperation::EraseRemoteRepositoryOperation(
    EraseRemoteRepositoryOperation&& other) = default;

EraseRemoteRepositoryOperation& EraseRemoteRepositoryOperation::operator=(
    EraseRemoteRepositoryOperation&& other) = default;

void EraseRemoteRepositoryOperation::Start(std::function<void(bool)> on_done) {
  FXL_DCHECK(!on_done_);
  on_done_ = std::move(on_done);
  FXL_DCHECK(on_done_);

  cloud_provider_->GetDeviceSet(device_set_.NewRequest(), [this](auto status) {
    if (status != cloud_provider::Status::OK) {
      FXL_LOG(ERROR)
          << "Failed to retrieve device set from the cloud provider.";
      on_done_(false);
      return;
    }
    ClearDeviceMap();
  });

  device_set_.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the device set "
                   << "while trying to erase the repository.";
    FXL_DCHECK(on_done_);
    on_done_(false);
  });
}

void EraseRemoteRepositoryOperation::ClearDeviceMap() {
  device_set_->Erase([this](auto status) {
    if (status != cloud_provider::Status::OK) {
      FXL_LOG(ERROR) << "Failed to erase the device map: " << status;
      on_done_(false);
      return;
    }
    FXL_LOG(INFO) << "Erased the device map, will clear the state next.";
    task_runner_->PostDelayedTask([this] { EraseRemote(); },
                                  fxl::TimeDelta::FromSeconds(3));

  });
}

void EraseRemoteRepositoryOperation::EraseRemote() {
  cloud_provider_->EraseAllData([this](auto status) {
    if (status != cloud_provider::Status::OK) {
      FXL_LOG(ERROR) << "Failed to erase the remote data: " << status;
      on_done_(false);
      return;
    }
    FXL_LOG(INFO) << "Erased remote data.";
    on_done_(true);
  });
}

}  // namespace ledger
