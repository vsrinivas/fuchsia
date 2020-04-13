// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/device_id_provider.h"

#include <optional>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"

namespace feedback {
namespace {

using Response = fuchsia::feedback::DeviceIdProvider_GetId_Response;
using Result = fuchsia::feedback::DeviceIdProvider_GetId_Result;
using Error = fuchsia::feedback::DeviceIdError;

// Reads a device id from the file at |path|. If the device id doesn't exist or is invalid, return
// a nullopt.
std::optional<std::string> ReadDeviceId(const std::string& path) {
  std::string id;
  if (files::ReadFileToString(path, &id) && uuid::IsValid(id)) {
    return id;
  }

  return std::nullopt;
}

// Creates a new device id and stores it at |path| if the file doesn't exist or contains an
// invalid id.
//
// The id is a 128-bit (pseudo) random UUID in the form of version 4 as described in RFC 4122,
// section 4.4.
std::optional<std::string> InitializeDeviceId(const std::string& path) {
  if (files::IsDirectory(path)) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Unable to initialize feedback id, '%s' is a directory",
                                        path.c_str());
    return std::nullopt;
  }

  if (const std::optional<std::string> read_id = ReadDeviceId(path); read_id.has_value()) {
    return read_id;
  }

  std::string new_id = uuid::Generate();
  if (!uuid::IsValid(new_id) || !files::WriteFile(path, new_id.c_str(), new_id.size())) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Cannot write device id '%s' to '%s'", new_id.c_str(),
                                        path.c_str());
    return std::nullopt;
  }

  return new_id;
}

}  // namespace

DeviceIdProvider::DeviceIdProvider(const std::string& path)
    : device_id_(InitializeDeviceId(path)) {}

std::optional<std::string> DeviceIdProvider::GetId() { return device_id_; }

void DeviceIdProvider::GetId(GetIdCallback callback) {
  Result result;
  if (device_id_.has_value()) {
    // We need to copy |device_id_| since Response::Response() requires a rvalue reference to
    // std::string in its constructor.
    Response response(std::string(device_id_.value()));
    result = Result::WithResponse(std::move(response));
  } else {
    result = Result::WithErr(Error::NOT_FOUND);
  }

  callback(std::move(result));
}

}  // namespace feedback
