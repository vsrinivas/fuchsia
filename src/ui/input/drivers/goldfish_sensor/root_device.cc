// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/root_device.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>

#include <fbl/auto_lock.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"
#include "src/ui/input/drivers/goldfish_sensor/goldfish_sensor-bind.h"
#include "src/ui/input/drivers/goldfish_sensor/input_device.h"
#include "src/ui/input/drivers/goldfish_sensor/parser.h"

namespace goldfish::sensor {
namespace {

const char* kPipeName = "pipe:qemud:sensors";
const char* kTag = "goldfish-sensor";

const std::map<uint64_t, InputDeviceInfo> kInputDevices = {
    {0x0001, {"acceleration", AccelerationInputDevice::Create}},
    {0x0002, {"gyroscope", GyroscopeInputDevice::Create}},
};

}  // namespace

// static
zx_status_t RootDevice::Create(void* ctx, zx_device_t* device) {
  auto sensor_root = std::make_unique<RootDevice>(device);
  zx_status_t status = sensor_root->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // Create and bind all sensor input devices.
  status = sensor_root->Setup(kInputDevices);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Cannot setup input devices, error %d", status);
    return status;
  }

  // the root device will be managed by devmgr.
  __UNUSED auto* dev = sensor_root.release();
  return ZX_OK;
}

RootDevice::RootDevice(zx_device_t* parent)
    : RootDeviceType(parent),
      pipe_(parent),
      input_dev_loop_(&kAsyncLoopConfigNeverAttachToThread),
      pipe_io_loop_(&kAsyncLoopConfigNeverAttachToThread) {
  input_dev_loop_.StartThread("input-devices-event-thread");
  pipe_io_loop_.StartThread("pipe-event-thread");
}

RootDevice::~RootDevice() {
  input_dev_loop_.Shutdown();
  pipe_io_loop_.Shutdown();
}

zx_status_t RootDevice::Setup(const std::map<uint64_t, InputDeviceInfo>& input_devices) {
  auto_reader_ =
      std::make_unique<PipeAutoReader>(&pipe_, kPipeName, pipe_io_loop_.dispatcher(),
                                       fit::bind_member(this, &RootDevice::OnReadSensor));
  if (!auto_reader_->valid()) {
    zxlogf(ERROR, "%s: PipeAutoReader() initialization failed", kTag);
    return ZX_ERR_INTERNAL;
  }

  // "list-sensors" returns a binary mask of all available sensors.
  auto_reader_->WriteWithHeader("list-sensors", /* blocking= */ true);
  auto result = auto_reader_->ReadWithHeader();
  uint32_t sensor_mask = 0u;
  if (result.is_ok()) {
    auto* sensor_mask_str = reinterpret_cast<const char*>(result.value().data());
    int size_read = sscanf(sensor_mask_str, "%d", &sensor_mask);
    if (size_read != 1) {
      zxlogf(ERROR, "Invalid list-sensors mask: %s", sensor_mask_str);
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    zxlogf(ERROR, "Cannot list sensors!");
    return ZX_ERR_INTERNAL;
  }

  for (const auto& kv : input_devices) {
    if (sensor_mask & kv.first) {
      auto create_result = kv.second.create_fn(this, input_dev_loop_.dispatcher());
      if (create_result.is_ok()) {
        input_devices_.AddDevice(create_result.value(), kv.second.name);

        char msg[256];
        snprintf(msg, sizeof(msg), "set:%s:1", kv.second.name.c_str());
        auto_reader_->WriteWithHeader(msg, /* blocking= */ true);
        zxlogf(INFO, "Created device: %s", kv.second.name.c_str());
      } else {
        zxlogf(ERROR, "Cannot create device: %s", kv.second.name.c_str());
        return create_result.error();
      }
    }
  }

  zx_status_t status = auto_reader_->BeginRead();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: BeginRead() failed: %d", kTag, status);
    return status;
  }
  return ZX_OK;
}

void RootDevice::OnReadSensor(PipeIo::ReadResult result) {
  if (result.is_error()) {
    zxlogf(INFO, "Pipe error: %s", zx_status_get_string(result.error()));
    return;
  }

  ZX_DEBUG_ASSERT(result.is_ok());
  const char* data = reinterpret_cast<const char*>(result.value().data());

  auto report = ParseSensorReport(data, result.value().size());

  // TODO(fxbug.dev/78205): Handle non-device report headers, e.g. "sync"
  // and "device-sync".
  input_devices_.DispatchReportToDevice(report.name, report);
}

zx_status_t RootDevice::Bind() { return DdkAdd(ddk::DeviceAddArgs("goldfish-sensor")); }

void RootDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void RootDevice::DdkRelease() { delete this; }

}  // namespace goldfish::sensor

static constexpr zx_driver_ops_t goldfish_sensor_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::sensor::RootDevice::Create;
  return ops;
}();

ZIRCON_DRIVER(goldfish_sensor, goldfish_sensor_driver_ops, "zircon", "0.1");

// clang-format on
