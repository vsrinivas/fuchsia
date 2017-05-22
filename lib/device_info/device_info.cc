// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/device_info/device_info.h"

#include "apps/modular/lib/util/filesystem.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/random/uuid.h"
#include "lib/ftl/strings/string_printf.h"

namespace modular {

constexpr char kDeviceInfoDirectory[] = "/data/modular/device";
constexpr char kDeviceIDFile[] = "/data/modular/device/%s.syncid";
constexpr char kSyncDeviceProfile[] = "/data/modular/device/profile_config.json";

std::string LoadDeviceProfile() {
  std::string device_profile;

  if (!files::IsDirectory(kDeviceInfoDirectory))
    files::CreateDirectory(kDeviceInfoDirectory);

  if (!files::ReadFileToString(kSyncDeviceProfile, &device_profile))
    device_profile = "{}";

  return device_profile;
}

// TODO(security): this is a temporary implementation. audit the revocability
// of this ID for syncing.
// TODO(zbowling): refactor to use flatbuffer for storage
std::string LoadDeviceID(const std::string& user) {
  std::string device_id;

  if (!files::IsDirectory(kDeviceInfoDirectory))
    files::CreateDirectory(kDeviceInfoDirectory);

  // FIXME(zbowling): this isn't scalable
  std::string path = ftl::StringPrintf(kDeviceIDFile, user.c_str());

  if (!files::ReadFileToString(path, &device_id)) {
    // no existing device id. generate a UUID and store it to disk
    device_id = ftl::GenerateUUID();
    bool success = files::WriteFile(path, device_id.data(), device_id.length());
    FTL_DCHECK(success);
  }

  FTL_LOG(INFO) << "device_info: syncing device id for user: " << user
                << "   set to: " << device_id;

  return device_id;
}

}  // namespace modular
