// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "lib/fit/promise.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/uuid/uuid.h"

namespace forensics::feedback {

RemoteDeviceIdProvider::RemoteDeviceIdProvider(async_dispatcher_t* dispatcher,
                                               std::shared_ptr<sys::ServiceDirectory> services)
    : connection_(dispatcher, services, [this] { MakeCall(); }) {}

::fpromise::promise<std::string, Error> RemoteDeviceIdProvider::GetId(const zx::duration timeout) {
  return connection_.GetValue(fit::Timeout(timeout));
}

void RemoteDeviceIdProvider::MakeCall() {
  connection_->GetId([this](std::string feedback_id) { connection_.SetValue(feedback_id); });
}

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

LocalDeviceIdProvider::LocalDeviceIdProvider(const std::string& path)
    : device_id_(InitializeDeviceId(path)) {}

::fpromise::promise<std::string, Error> LocalDeviceIdProvider::GetId(zx::duration timeout) {
  return ::fpromise::make_result_promise<std::string, Error>(::fit::ok(device_id_));
}

}  // namespace forensics::feedback
