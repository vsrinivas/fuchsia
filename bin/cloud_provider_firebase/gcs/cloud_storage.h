// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_H_

#include <functional>
#include <string>

#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firebase/gcs/status.h"

namespace gcs {

class CloudStorage {
 public:
  CloudStorage(){};
  virtual ~CloudStorage(){};

  virtual void UploadObject(std::string auth_token, const std::string& key,
                            fsl::SizedVmo data,
                            fit::function<void(Status)> callback) = 0;

  virtual void DownloadObject(
      std::string auth_token, const std::string& key,
      fit::function<void(Status status, uint64_t size, zx::socket data)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudStorage);
};

}  // namespace gcs

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_GCS_CLOUD_STORAGE_H_
