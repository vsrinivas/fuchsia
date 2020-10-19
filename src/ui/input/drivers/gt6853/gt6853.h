// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_
#define SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>

#include "src/ui/input/lib/input-report-reader/reader.h"

namespace touch {

constexpr uint32_t kMaxContacts = 10;

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

struct Gt6853Contact {
  uint32_t contact_id;
  int64_t position_x;
  int64_t position_y;
};

struct Gt6853InputReport {
  zx::time event_time;
  Gt6853Contact contacts[kMaxContacts];
  size_t num_contacts;

  void ToFidlInputReport(fuchsia_input_report::InputReport::Builder& builder,
                         fidl::Allocator& allocator);
};

class Gt6853Device;
using DeviceType = ddk::Device<Gt6853Device, ddk::Messageable, ddk::Unbindable>;

class Gt6853Device : public DeviceType,
                     fuchsia_input_report::InputDevice::Interface,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  Gt6853Device(zx_device_t* parent, ddk::I2cChannel i2c) : Gt6853Device(parent, i2c, {}, {}) {}

  Gt6853Device(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient interrupt_gpio,
               ddk::GpioProtocolClient reset_gpio)
      : DeviceType(parent),
        i2c_(i2c),
        interrupt_gpio_(interrupt_gpio),
        reset_gpio_(reset_gpio),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~Gt6853Device() override = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  // Visible for testing.
  static zx::status<Gt6853Device*> CreateAndGetDevice(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn);

  void GetInputReportsReader(zx::channel server,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(fuchsia_input_report::OutputReport report,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(fuchsia_input_report::FeatureReport report,
                        SetFeatureReportCompleter::Sync& completer) override;

  // Visible for testing.
  void WaitForNextReader();

 private:
  enum class Register : uint16_t;

  static Gt6853Contact ParseContact(const uint8_t* contact_buffer);

  zx_status_t Init();

  zx::status<uint8_t> ReadReg8(Register reg);
  zx::status<> Read(Register reg, uint8_t* buffer, size_t size);
  zx::status<> WriteReg8(Register reg, uint8_t value);

  int Thread();
  void Shutdown();  // Only called after thread_ has been started.

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient interrupt_gpio_;
  ddk::GpioProtocolClient reset_gpio_;
  zx::interrupt interrupt_;

  thrd_t thread_ = {};

  input::InputReportReaderManager<Gt6853InputReport> input_report_readers_;
  sync_completion_t next_reader_wait_;
  async::Loop loop_;
};

}  // namespace touch

#endif  // SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_
