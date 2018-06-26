// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_IMPL_H_

#include <functional>
#include <vector>

#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include "lib/callback/cancellable.h"
#include "lib/network_wrapper/network_wrapper.h"
#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage.h"

namespace gcs {

// Implementation of the CloudStorage interface that uses Firebase Storage as
// the backend.
class CloudStorageImpl : public CloudStorage {
 public:
  CloudStorageImpl(network_wrapper::NetworkWrapper* network_wrapper,
                   const std::string& firebase_id,
                   const std::string& cloud_prefix);
  ~CloudStorageImpl() override;

  // CloudStorage implementation.
  void UploadObject(std::string auth_token, const std::string& key,
                    fsl::SizedVmo data,
                    std::function<void(Status)> callback) override;

  void DownloadObject(
      std::string auth_token, const std::string& key,
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback) override;

 private:
  std::string GetDownloadUrl(fxl::StringView key);

  std::string GetUploadUrl(fxl::StringView key);

  void Request(
      std::function<::fuchsia::net::oldhttp::URLRequest()> request_factory,
      std::function<void(Status status,
                         ::fuchsia::net::oldhttp::URLResponse response)>
          callback);
  void OnResponse(
      std::function<void(Status status,
                         ::fuchsia::net::oldhttp::URLResponse response)>
          callback,
      ::fuchsia::net::oldhttp::URLResponse response);

  void OnDownloadResponseReceived(
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback,
      Status status, ::fuchsia::net::oldhttp::URLResponse response);

  network_wrapper::NetworkWrapper* const network_wrapper_;
  const std::string url_prefix_;
  callback::CancellableContainer requests_;
};

}  // namespace gcs

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_IMPL_H_
