// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_GCS_CLOUD_STORAGE_IMPL_H_
#define APPS_LEDGER_GCS_CLOUD_STORAGE_IMPL_H_

#include <functional>
#include <vector>

#include "apps/ledger/gcs/cloud_storage.h"
#include "apps/network/interfaces/network_service.mojom.h"
#include "lib/ftl/tasks/task_runner.h"

namespace gcs {

class CloudStorageImpl : public CloudStorage {
 public:
  CloudStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                   mojo::NetworkServicePtr network_service,
                   const std::string& bucket_name);
  ~CloudStorageImpl() override;

  // CloudStorage implementation.
  void UploadFile(const std::string& key,
                  const std::string& source,
                  const std::function<void(Status)>& callback) override;
  void DownloadFile(const std::string& key,
                    const std::string& destination,
                    const std::function<void(Status)>& callback) override;

 private:
  void Request(
      mojo::URLRequestPtr request,
      const std::function<void(Status status, mojo::URLResponsePtr response)>&
          callback);
  void OnResponse(
      const std::function<void(Status status, mojo::URLResponsePtr response)>&
          callback,
      mojo::URLLoader* url_loader,
      mojo::URLResponsePtr response);

  void OnDownloadResponseReceived(const std::string& destination,
                                  const std::function<void(Status)>& callback,
                                  Status status,
                                  mojo::URLResponsePtr response);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  mojo::NetworkServicePtr network_service_;
  std::string bucket_name_;
  std::vector<mojo::URLLoaderPtr> loaders_;
};

}  // gcs

#endif  // APPS_LEDGER_GCS_CLOUD_STORAGE_IMPL_H_
