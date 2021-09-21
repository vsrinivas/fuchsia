// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_
#define SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>
#include <time.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/mutex.h>

#include "src/ui/input/lib/input-report-reader/reader.h"

namespace tcs {

struct Tcs3400InputReport {
  zx::time event_time = zx::time(ZX_TIME_INFINITE_PAST);
  int64_t illuminance;
  int64_t red;
  int64_t blue;
  int64_t green;

  void ToFidlInputReport(fuchsia_input_report::wire::InputReport& input_report,
                         fidl::AnyArena& allocator);

  bool is_valid() const { return event_time.get() != ZX_TIME_INFINITE_PAST; }
};

struct Tcs3400FeatureReport {
  int64_t report_interval_us;
  fuchsia_input_report::wire::SensorReportingState reporting_state;
  int64_t sensitivity;
  int64_t threshold_high;
  int64_t threshold_low;

  fuchsia_input_report::wire::FeatureReport ToFidlFeatureReport(fidl::AnyArena& allocator) const;
};

class Tcs3400Device;

class Tcs3400Device;
using DeviceType =
    ddk::Device<Tcs3400Device, ddk::Messageable<fuchsia_input_report::InputDevice>::Mixin,
                ddk::Unbindable>;

class Tcs3400Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Visible for testing.
  static zx::status<Tcs3400Device*> CreateAndGetDevice(void* ctx, zx_device_t* parent);

  Tcs3400Device(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient gpio,
                zx::port port)
      : DeviceType(device),
        i2c_(i2c),
        gpio_(gpio),
        port_(std::move(port)),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~Tcs3400Device() override = default;

  zx_status_t Bind();
  zx_status_t InitMetadata();

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;

  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportRequestView request,
                        GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override;
  void GetInputReport(GetInputReportRequestView request,
                      GetInputReportCompleter::Sync& completer) override;

  // Visible for testing.
  void WaitForNextReader();
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

 private:
  static constexpr size_t kFeatureAndDescriptorBufferSize = 512;

  ddk::I2cChannel i2c_;  // Accessed by the main thread only before thread_ has been started.
  ddk::GpioProtocolClient gpio_;
  zx::interrupt irq_;
  thrd_t thread_ = {};
  zx::port port_;
  fbl::Mutex input_lock_;
  fbl::Mutex feature_lock_;
  Tcs3400InputReport input_rpt_ TA_GUARDED(input_lock_) = {};
  Tcs3400FeatureReport feature_rpt_ TA_GUARDED(feature_lock_) = {};
  uint8_t atime_ = 1;
  uint8_t again_ = 1;
  bool isSaturated_ = false;
  time_t lastSaturatedLog_ = 0;
  sync_completion_t next_reader_wait_;
  async::Loop loop_;
  input::InputReportReaderManager<Tcs3400InputReport> readers_;

  zx::status<Tcs3400InputReport> ReadInputRpt();
  zx_status_t InitGain(uint8_t gain);
  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  zx_status_t ReadReg(uint8_t reg, uint8_t& output_value);
  int Thread();
  void ShutDown();
};
}  // namespace tcs

#endif  // SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_
