// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/device_id_provider.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

// Server for |fuchsia.feedback.DeviceIdProvider| that only responds to the first call to GetId as
// the ID never changes and the method is a hanging get.
class DeviceIdProvider : public fuchsia::feedback::DeviceIdProvider {
 public:
  DeviceIdProvider(async_dispatcher_t* dispatcher,
                   ::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request,
                   ::fit::function<void(zx_status_t)> on_channel_close, std::string* device_id)
      : connection_(this, std::move(request), dispatcher),
        has_been_called_(false),
        device_id_(device_id) {
    connection_.set_error_handler(std::move(on_channel_close));
  }

  // |fuchsia.feedback.DeviceIdProvider|
  void GetId(GetIdCallback callback) override {
    // This is the second call on this connection and we want to leave it hanging forever.
    if (has_been_called_) {
      return;
    }

    callback(*device_id_);
    has_been_called_ = true;
  }

 private:
  ::fidl::Binding<fuchsia::feedback::DeviceIdProvider> connection_;

  bool has_been_called_;
  std::string* device_id_;
};

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

DeviceIdManager::DeviceIdManager(async_dispatcher_t* dispatcher, const std::string& path)
    : dispatcher_(dispatcher), device_id_(InitializeDeviceId(path)), next_provider_idx_(0u) {}

void DeviceIdManager::AddBinding(
    ::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request,
    ::fit::function<void(zx_status_t)> on_channel_close) {
  const size_t idx = next_provider_idx_++;
  providers_.emplace(
      idx,
      std::make_unique<DeviceIdProvider>(
          dispatcher_, std::move(request),
          [this, idx, on_channel_close = std::move(on_channel_close)](const zx_status_t status) {
            // Execute |on_channel_close| before removing the created DeviceIdProvider from
            // |providers_|.
            on_channel_close(status);
            providers_.erase(idx);
          },
          &device_id_));
}

}  // namespace feedback_data
}  // namespace forensics
