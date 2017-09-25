// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_GCS_CLOUD_STORAGE_H_
#define PERIDOT_BIN_LEDGER_GCS_CLOUD_STORAGE_H_

#include <functional>
#include <string>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/gcs/status.h"
#include "zx/socket.h"
#include "zx/vmo.h"

namespace gcs {

class CloudStorage {
 public:
  CloudStorage(){};
  virtual ~CloudStorage(){};

  virtual void UploadObject(std::string auth_token,
                            const std::string& key,
                            zx::vmo data,
                            std::function<void(Status)> callback) = 0;

  virtual void DownloadObject(
      std::string auth_token,
      const std::string& key,
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CloudStorage);
};

}  // namespace gcs

#endif  // PERIDOT_BIN_LEDGER_GCS_CLOUD_STORAGE_H_
