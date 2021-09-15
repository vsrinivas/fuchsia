// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_H_
#define SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_H_
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/result.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "lib/fidl/llcpp/arena.h"
#include "src/ui/input/drivers/goldfish_sensor/parser.h"
#include "src/ui/input/lib/input-report-reader/reader.h"

namespace goldfish::sensor {

class InputDevice;
using InputDeviceType =
    ddk::Device<InputDevice, ddk::Messageable<fuchsia_input_report::InputDevice>::Mixin,
                ddk::Unbindable>;

class RootDevice;

using InputDeviceCreateFunc =
    fit::result<InputDevice*, zx_status_t> (*)(RootDevice* parent, async_dispatcher_t* dispatcher);

struct InputDeviceInfo {
  std::string name;
  InputDeviceCreateFunc create_fn;
};

// A goldfish multisensor device may create multiple sensors. Each sensor
// corresponds to an InputDevice serving fuchsia.input.report.InputDevice FIDL
// protocol and could be accessed at /dev/class/input-report/<id>.
class InputDevice : public InputDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  using OnDestroyCallback = fit::function<void(InputDevice*)>;
  InputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher, OnDestroyCallback on_destroy);

  ~InputDevice() override;

  void DdkRelease() { delete this; }
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

  virtual zx_status_t OnReport(const SensorReport& rpt) = 0;

  // Open a new InputReportsReader on this device.
  // Since each device has its own report format, each device needs to keep
  // its own InputReportManager and implement its own GetInputReportsReader().
  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override = 0;

  // Gets the device descriptor for this device.
  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override = 0;

  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  // TODO(fxbug.dev/78205): Support feature reports (polling frequency,
  // sensor value thresholds).
  void GetFeatureReport(GetFeatureReportRequestView request,
                        GetFeatureReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  // TODO(fxbug.dev/78205): Support feature reports (polling frequency,
  // sensor value thresholds).
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

 private:
  async_dispatcher_t* dispatcher_;
  OnDestroyCallback on_destroy_ = nullptr;
};

class AccelerationInputDevice : public InputDevice {
 public:
  struct InputReport {
    float x, y, z;
    zx::time event_time;

    void ToFidlInputReport(fuchsia_input_report::wire::InputReport& input_report,
                           fidl::AnyArena& allocator);
  };

  // Creates an AccelerationInputDevice. Takes an unowned pointer to |parent|.
  // |parent| must outlive the device that's created.
  static fit::result<InputDevice*, zx_status_t> Create(RootDevice* parent,
                                                       async_dispatcher_t* dispatcher);

  AccelerationInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher,
                          OnDestroyCallback on_destroy)
      : InputDevice(parent, dispatcher, std::move(on_destroy)) {}

  zx_status_t OnReport(const SensorReport& rpt) override;

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override;

 private:
  input::InputReportReaderManager<InputReport> input_report_readers_;
};

class GyroscopeInputDevice : public InputDevice {
 public:
  struct InputReport {
    float x, y, z;
    zx::time event_time;

    void ToFidlInputReport(fuchsia_input_report::wire::InputReport& input_report,
                           fidl::AnyArena& allocator);
  };

  static fit::result<InputDevice*, zx_status_t> Create(RootDevice* parent,
                                                       async_dispatcher_t* dispatcher);

  // Creates an GyroscopeInputDevice. Takes an unowned pointer to |parent|.
  // |parent| must outlive the device that's created.
  GyroscopeInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher,
                       OnDestroyCallback on_destroy)
      : InputDevice(parent, dispatcher, std::move(on_destroy)) {}

  zx_status_t OnReport(const SensorReport& rpt) override;

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;

  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override;

 private:
  input::InputReportReaderManager<InputReport> input_report_readers_;
};

class RgbcLightInputDevice : public InputDevice {
 public:
  struct InputReport {
    float r, g, b, c;
    zx::time event_time;

    void ToFidlInputReport(fuchsia_input_report::wire::InputReport& input_report,
                           fidl::AnyArena& allocator);
  };

  static fit::result<InputDevice*, zx_status_t> Create(RootDevice* parent,
                                                       async_dispatcher_t* dispatcher);

  // Creates a RgbcLightInputDevice. Takes an unowned pointer to |parent|.
  // |parent| must outlive the device that's created.
  RgbcLightInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher,
                       OnDestroyCallback on_destroy)
      : InputDevice(parent, dispatcher, std::move(on_destroy)) {}

  zx_status_t OnReport(const SensorReport& rpt) override;

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;

  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override;

 private:
  input::InputReportReaderManager<InputReport> input_report_readers_;
};

}  // namespace goldfish::sensor

#endif  // SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_H_
