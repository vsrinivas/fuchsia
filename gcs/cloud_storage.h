// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_GCS_CLOUD_STORAGE_H_
#define APPS_LEDGER_GCS_CLOUD_STORAGE_H_

#include <functional>
#include <string>

#include "lib/ftl/macros.h"

namespace gcs {

enum class Status {
  OK,
  OBJECT_ALREADY_EXIST,
  UNKNOWN_ERROR,
};

class CloudStorage {
 public:
  CloudStorage(){};
  virtual ~CloudStorage(){};

  virtual void UploadFile(const std::string& key,
                          const std::string& source,
                          const std::function<void(Status)>& callback) = 0;

  virtual void DownloadFile(const std::string& key,
                            const std::string& destination,
                            const std::function<void(Status)>& callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudStorage);
};

}  // gcs

#endif  // APPS_LEDGER_GCS_CLOUD_STORAGE_H_
