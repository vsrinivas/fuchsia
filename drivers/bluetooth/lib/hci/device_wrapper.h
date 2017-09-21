// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {

// A DeviceWrapper abstracts over a Bluetooth HCI device object and its ioctls.
class DeviceWrapper {
 public:
  virtual ~DeviceWrapper() = default;

  // Returns the command/event channel handle for this device. Returns an invalid handle on failure.
  virtual zx::channel GetCommandChannel() = 0;

  // Returns the ACL data channel handle for this device. Returns an invalid handle on failure.
  virtual zx::channel GetACLDataChannel() = 0;
};

// A DeviceWrapper that works over a Zircon bt-hci device.
class ZirconDeviceWrapper : public DeviceWrapper {
 public:
  // |device_fd| must be a valid file descriptor to a Bluetooth HCI device.
  explicit ZirconDeviceWrapper(fxl::UniqueFD device_fd);
  ~ZirconDeviceWrapper() override = default;

  // DeviceWrapper overrides. These methods directly return the handle obtained via the
  // corresponding ioctl.
  zx::channel GetCommandChannel() override;
  zx::channel GetACLDataChannel() override;

 private:
  fxl::UniqueFD device_fd_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ZirconDeviceWrapper);
};

// A pass-through DeviceWrapper that returns the channel endpoints that it is initialized with. This
// is useful for test scenarios.
class DummyDeviceWrapper : public DeviceWrapper {
 public:
  // The constructor takes ownership of the provided channels and simply returns them when asked for
  // them.
  DummyDeviceWrapper(zx::channel cmd_channel, zx::channel acl_data_channel);
  ~DummyDeviceWrapper() override = default;

  // DeviceWrapper overrides. Since these methods simply forward the handles they were initialized
  // with, the internal handles will be moved and invalidated after the first call to these methods.
  // Subsequent calls will always return an invalid handle.
  zx::channel GetCommandChannel() override;
  zx::channel GetACLDataChannel() override;

 private:
  zx::channel cmd_channel_;
  zx::channel acl_data_channel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceWrapper);
};

}  // namespace hci
}  // namespace bluetooth
