// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GCS_CLOUD_STORAGE_IMPL_H_
#define APPS_LEDGER_SRC_GCS_CLOUD_STORAGE_IMPL_H_

#include <functional>
#include <vector>

#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "apps/ledger/src/network/network_service.h"
#include "lib/fxl/tasks/task_runner.h"
#include "mx/socket.h"
#include "mx/vmo.h"

namespace gcs {

// Implementation of the CloudStorage interface that uses Firebase Storage as
// the backend.
class CloudStorageImpl : public CloudStorage {
 public:
  CloudStorageImpl(fxl::RefPtr<fxl::TaskRunner> task_runner,
                   ledger::NetworkService* network_service,
                   const std::string& firebase_id,
                   const std::string& cloud_prefix);
  ~CloudStorageImpl() override;

  // CloudStorage implementation.
  void UploadObject(std::string auth_token,
                    const std::string& key,
                    mx::vmo data,
                    std::function<void(Status)> callback) override;

  void DownloadObject(
      std::string auth_token,
      const std::string& key,
      std::function<void(Status status, uint64_t size, mx::socket data)>
          callback) override;

 private:
  std::string GetDownloadUrl(fxl::StringView key);

  std::string GetUploadUrl(fxl::StringView key);

  void Request(std::function<network::URLRequestPtr()> request_factory,
               std::function<void(Status status,
                                  network::URLResponsePtr response)> callback);
  void OnResponse(
      std::function<void(Status status, network::URLResponsePtr response)>
          callback,
      network::URLResponsePtr response);

  void OnDownloadResponseReceived(
      std::function<void(Status status, uint64_t size, mx::socket data)>
          callback,
      Status status,
      network::URLResponsePtr response);

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  ledger::NetworkService* const network_service_;
  const std::string url_prefix_;
  callback::CancellableContainer requests_;
};

}  // namespace gcs

#endif  // APPS_LEDGER_SRC_GCS_CLOUD_STORAGE_IMPL_H_
