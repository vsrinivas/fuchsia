// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

// Reads a device id from the file at |path|. If the device id doesn't exist or is invalid, return
// a nullopt.
std::optional<std::string> ReadDeviceId(const std::string& path) {
  std::string id;
  if (!files::ReadFileToString(path, &id)) {
    return std::nullopt;
  }

  return id;
}

// Creates a new device id and stores it at |path|.
//
// The id is a 128-bit (pseudo) random UUID in the form of version 4 as described in RFC 4122,
// section 4.4.
std::string InitializeDeviceId(const std::string& path) {
  if (const auto read_id = ReadDeviceId(path);
      read_id.has_value() && uuid::IsValid(read_id.value())) {
    return read_id.value();
  }

  std::string new_id = uuid::Generate();
  if (!files::WriteFile(path, new_id.c_str(), new_id.size())) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Cannot write device id '%s' to '%s'", new_id.c_str(),
                                        path.c_str());
  }

  FX_LOGS(INFO) << "Created new feedback device id";
  return new_id;
}

}  // namespace

DeviceIdProvider::DeviceIdProvider(const std::string& path)
    : device_id_(InitializeDeviceId(path)) {}

void DeviceIdProvider::GetId(GetIdCallback callback) { callback(device_id_); }

}  // namespace feedback_data
}  // namespace forensics
