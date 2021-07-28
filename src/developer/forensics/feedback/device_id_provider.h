// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/hanging_get_ptr.h"

namespace forensics::feedback {

class DeviceIdProvider {
 public:
  virtual ~DeviceIdProvider() = default;
  virtual ::fpromise::promise<std::string, Error> GetId(zx::duration timeout) = 0;
};

// Fetches the device id from a FIDL server.
class RemoteDeviceIdProvider : public DeviceIdProvider {
 public:
  // fuchsia.feedback.DeviceIdProvider is expected to be in |services|.
  RemoteDeviceIdProvider(async_dispatcher_t* dispatcher,
                         std::shared_ptr<sys::ServiceDirectory> services);
  ~RemoteDeviceIdProvider() override = default;

  ::fpromise::promise<std::string, Error> GetId(zx::duration timeout) override;

 private:
  // Makes the unique call on |connection_|.
  void MakeCall();

  fidl::HangingGetPtr<fuchsia::feedback::DeviceIdProvider, std::string> connection_;
};

// Fetches the device id from the file at |path|.
class LocalDeviceIdProvider : public DeviceIdProvider {
 public:
  explicit LocalDeviceIdProvider(const std::string& path);
  ~LocalDeviceIdProvider() override = default;

  ::fpromise::promise<std::string, Error> GetId(zx::duration timeout) override;

 private:
  std::string device_id_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DEVICE_ID_PROVIDER_H_
