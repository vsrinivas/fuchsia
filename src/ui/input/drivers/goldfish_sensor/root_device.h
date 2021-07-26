// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_ROOT_DEVICE_H_
#define SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_ROOT_DEVICE_H_

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>

#include <memory>
#include <unordered_map>

#include <ddktl/device.h>
#include <fbl/macros.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_auto_reader.h"
#include "src/ui/input/drivers/goldfish_sensor/input_device.h"
#include "src/ui/input/drivers/goldfish_sensor/input_device_dispatcher.h"

namespace goldfish::sensor {

class RootDevice;
using RootDeviceType = ddk::Device<RootDevice, ddk::Unbindable>;

// A goldfish multisensor device manages multiple sensor InputDevices.
// It reads all raw goldfish pipe input on "goldfish:qemud:sensor" pipe,
// converts them into sensor report formats, and dispatch to corresponding
// sensor devices.
class RootDevice : public RootDeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit RootDevice(zx_device_t* parent);
  ~RootDevice();

  zx_status_t Bind();

  // Initialize goldfish pipe reader, get a binary mask of all available
  // sensors, and create an input device for each sensor available.
  // Returns:
  // - |ZX_OK| if both sensor query and creation succeed.
  // - |ZX_ERR_INTERNAL| if cannot read from goldfish pipe.
  // - |ZX_ERR_INVALID_ARGS| if available sensor mask is invalid.
  zx_status_t Setup(const std::map<uint64_t, InputDeviceInfo>& input_devices);

  // Ddk mixin implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  InputDeviceDispatcher* input_devices() { return &input_devices_; }

 protected:
  // Set to protected to allow test devices to use this method.
  void OnReadSensor(PipeIo::ReadResult result);

 private:
  ddk::GoldfishPipeProtocolClient pipe_;
  std::unique_ptr<PipeAutoReader> auto_reader_;
  InputDeviceDispatcher input_devices_;

  async::Loop input_dev_loop_;
  async::Loop pipe_io_loop_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RootDevice);
};

}  // namespace goldfish::sensor

#endif  // SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_ROOT_DEVICE_H_
