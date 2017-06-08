// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/local_version_checker.h"

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/concatenate.h"

namespace cloud_sync {

namespace {
constexpr size_t kDeviceIdSize = 16;

std::string GetMetaDataKey(ftl::StringView local_version) {
  return ftl::Concatenate({"__metadata/devices/", local_version});
}
}  // namespace

LocalVersionChecker::LocalVersionChecker() {}

LocalVersionChecker::~LocalVersionChecker() {}

void LocalVersionChecker::CheckCloudVersion(
    std::string auth_token,
    firebase::Firebase* user_firebase,
    std::string local_version_path,
    std::function<void(Status)> callback) {
  std::vector<std::string> query_params;
  if (!auth_token.empty()) {
    query_params.push_back("auth=" + auth_token);
  }

  if (files::IsFile(local_version_path)) {
    // A local version exists. Check if it is compatible with the cloud version.
    std::string local_version;
    if (!files::ReadFileToString(local_version_path, &local_version)) {
      FTL_LOG(ERROR) << "Unable to read local file at path: "
                     << local_version_path << ".";
      callback(Status::DISK_ERROR);
      return;
    }

    user_firebase->Get(
        GetMetaDataKey(local_version), query_params,
        [callback = std::move(callback)](firebase::Status status,
                                         const rapidjson::Value& value) {
          if (status != firebase::Status::OK) {
            FTL_LOG(WARNING) << "Unable to read version from the cloud.";
            callback(Status::NETWORK_ERROR);
            return;
          }

          if (value.IsNull()) {
            callback(Status::INCOMPATIBLE);
            return;
          }

          // If metadata are present, the version on the cloud is compatible.
          callback(Status::OK);
        });

    return;
  }

  // There is not local version. Create one.
  char local_version_array[kDeviceIdSize];
  glue::RandBytes(local_version_array, kDeviceIdSize);
  std::string local_version =
      convert::ToHex(ftl::StringView(local_version_array, kDeviceIdSize));
  std::string firebase_key = GetMetaDataKey(local_version);
  user_firebase->Put(firebase_key, query_params, "true", [
    local_version_path = std::move(local_version_path),
    local_version = std::move(local_version), callback = std::move(callback)
  ](firebase::Status status) {
    if (status != firebase::Status::OK) {
      FTL_LOG(WARNING) << "Unable to set local version on the cloud.";
      callback(Status::NETWORK_ERROR);
      return;
    }

    if (!files::WriteFile(local_version_path, local_version.data(),
                          local_version.size())) {
      FTL_LOG(WARNING) << "Unable to persist local version to disk at: "
                       << local_version_path << ".";
      callback(Status::DISK_ERROR);
      return;
    }

    callback(Status::OK);
  });
}

}  // namespace cloud_sync
