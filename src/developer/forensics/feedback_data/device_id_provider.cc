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

using Response = fuchsia::feedback::DeviceIdProvider_GetId_Response;
using Result = fuchsia::feedback::DeviceIdProvider_GetId_Result;
using DeviceIdError = fuchsia::feedback::DeviceIdError;

// Reads a device id from the file at |path|. If the device id doesn't exist or is invalid, return
// a nullopt.
AnnotationOr ReadDeviceId(const std::string& path) {
  std::string id;
  if (!files::ReadFileToString(path, &id)) {
    return AnnotationOr(Error::kFileReadFailure);
  }

  return (uuid::IsValid(id)) ? AnnotationOr(id) : AnnotationOr(Error::kBadValue);
}

// Creates a new device id and stores it at |path| if the file doesn't exist or contains an
// invalid id.
//
// The id is a 128-bit (pseudo) random UUID in the form of version 4 as described in RFC 4122,
// section 4.4.
AnnotationOr InitializeDeviceId(const std::string& path) {
  if (files::IsDirectory(path)) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Unable to initialize feedback id, '%s' is a directory",
                                        path.c_str());
    return AnnotationOr(Error::kFileReadFailure);
  }

  if (const AnnotationOr read_id = ReadDeviceId(path); read_id.HasValue()) {
    return AnnotationOr(read_id);
  }

  std::string new_id = uuid::Generate();
  if (!uuid::IsValid(new_id)) {
    FX_LOGS(ERROR) << fxl::StringPrintf("%s is not a valid feedback id", new_id.c_str());
    return AnnotationOr(Error::kBadValue);
  }

  if (!files::WriteFile(path, new_id.c_str(), new_id.size())) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Cannot write device id '%s' to '%s'", new_id.c_str(),
                                        path.c_str());
    return AnnotationOr(Error::kFileWriteFailure);
  }

  FX_LOGS(INFO) << "Successfully created new feedback device id";
  return AnnotationOr(new_id);
}

}  // namespace

DeviceIdProvider::DeviceIdProvider(const std::string& path)
    : device_id_(InitializeDeviceId(path)) {}

AnnotationOr DeviceIdProvider::GetId() { return device_id_; }

void DeviceIdProvider::GetId(GetIdCallback callback) {
  Result result;
  if (device_id_.HasValue()) {
    // We need to copy |device_id_| since Response::Response() requires a rvalue reference to
    // std::string in its constructor.
    Response response(std::string(device_id_.Value()));
    result = Result::WithResponse(std::move(response));
  } else {
    result = Result::WithErr(DeviceIdError::NOT_FOUND);
  }

  callback(std::move(result));
}

}  // namespace feedback_data
}  // namespace forensics
