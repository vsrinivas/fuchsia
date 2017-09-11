// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/channel.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {

// A DeviceWrapper abstracts over a Bluetooth HCI device object and its ioctls.
class DeviceWrapper {
 public:
  virtual ~DeviceWrapper() = default;

  // Returns the command/event channel handle for this device. Returns an invalid handle on failure.
  virtual mx::channel GetCommandChannel() = 0;

  // Returns the ACL data channel handle for this device. Returns an invalid handle on failure.
  virtual mx::channel GetACLDataChannel() = 0;
};

// A DeviceWrapper that works over a Magenta bt-hci device.
class MagentaDeviceWrapper : public DeviceWrapper {
 public:
  // |device_fd| must be a valid file descriptor to a Bluetooth HCI device.
  explicit MagentaDeviceWrapper(fxl::UniqueFD device_fd);
  ~MagentaDeviceWrapper() override = default;

  // DeviceWrapper overrides. These methods directly return the handle obtained via the
  // corresponding ioctl.
  mx::channel GetCommandChannel() override;
  mx::channel GetACLDataChannel() override;

 private:
  fxl::UniqueFD device_fd_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MagentaDeviceWrapper);
};

// A pass-through DeviceWrapper that returns the channel endpoints that it is initialized with. This
// is useful for test scenarios.
class DummyDeviceWrapper : public DeviceWrapper {
 public:
  // The constructor takes ownership of the provided channels and simply returns them when asked for
  // them.
  DummyDeviceWrapper(mx::channel cmd_channel, mx::channel acl_data_channel);
  ~DummyDeviceWrapper() override = default;

  // DeviceWrapper overrides. Since these methods simply forward the handles they were initialized
  // with, the internal handles will be moved and invalidated after the first call to these methods.
  // Subsequent calls will always return an invalid handle.
  mx::channel GetCommandChannel() override;
  mx::channel GetACLDataChannel() override;

 private:
  mx::channel cmd_channel_;
  mx::channel acl_data_channel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceWrapper);
};

}  // namespace hci
}  // namespace bluetooth
