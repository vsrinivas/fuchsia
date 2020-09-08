// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_FT8201_FT8201_H_
#define SRC_UI_INPUT_DRIVERS_FT8201_FT8201_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <threads.h>

#include <list>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>

namespace touch {

constexpr uint32_t kNumContacts = 10;

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

struct Ft8201Contact {
  uint32_t contact_id;
  int64_t position_x;
  int64_t position_y;
  int64_t pressure;
};

struct Ft8201InputReport {
  zx::time event_time;
  Ft8201Contact contacts[kNumContacts];
  size_t num_contacts;

  fuchsia_input_report::InputReport ToFidlInputReport(fidl::Allocator& allocator);
};

class Ft8201Device;

class Ft8201InputReportsReader : public fuchsia_input_report::InputReportsReader::Interface {
 public:
  static std::unique_ptr<Ft8201InputReportsReader> Create(Ft8201Device* base,
                                                          async_dispatcher_t* dispatcher,
                                                          zx::channel server);

  explicit Ft8201InputReportsReader(Ft8201Device* const base) : base_(base) {}
  ~Ft8201InputReportsReader() override = default;

  void ReadInputReports(ReadInputReportsCompleter::Sync completer) TA_EXCL(&report_lock_) override;

  void ReceiveReport(const Ft8201InputReport& report) TA_EXCL(&report_lock_);

 private:
  static constexpr size_t kInputReportBufferSize = 4096 * 4;

  void ReplyWithReports(ReadInputReportsCompleterBase& completer) TA_REQ(&report_lock_);

  fbl::Mutex report_lock_;
  std::optional<ReadInputReportsCompleter::Async> completer_ TA_GUARDED(&report_lock_);
  fidl::BufferThenHeapAllocator<kInputReportBufferSize> report_allocator_
      __TA_GUARDED(report_lock_);
  fbl::RingBuffer<Ft8201InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT> reports_data_
      __TA_GUARDED(report_lock_);

  Ft8201Device* const base_;
};

class Ft8201Device;
using DeviceType = ddk::Device<Ft8201Device, ddk::Messageable, ddk::UnbindableNew>;

class Ft8201Device : public DeviceType,
                     fuchsia_input_report::InputDevice::Interface,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  explicit Ft8201Device(zx_device_t* parent, ddk::I2cChannel i2c,
                        ddk::GpioProtocolClient interrupt_gpio, ddk::GpioProtocolClient reset_gpio)
      : DeviceType(parent),
        i2c_(i2c),
        interrupt_gpio_(interrupt_gpio),
        reset_gpio_(reset_gpio),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~Ft8201Device() override = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Visible for testing.
  static zx::status<Ft8201Device*> CreateAndGetDevice(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);

  void GetInputReportsReader(zx::channel server,
                             GetInputReportsReaderCompleter::Sync completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync completer) override;
  void SendOutputReport(fuchsia_input_report::OutputReport report,
                        SendOutputReportCompleter::Sync completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync completer) override;
  void SetFeatureReport(fuchsia_input_report::FeatureReport report,
                        SetFeatureReportCompleter::Sync completer) override;

  void RemoveReaderFromList(Ft8201InputReportsReader* reader) TA_EXCL(readers_lock_);

  // Visible for testing.
  void WaitForNextReader();

 private:
  static Ft8201Contact ParseContact(const uint8_t* contact_buffer);

  zx_status_t Init();
  int Thread();
  void Shutdown();  // Only called after thread_ has been started.

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient interrupt_gpio_;
  ddk::GpioProtocolClient reset_gpio_;
  zx::interrupt interrupt_;

  thrd_t thread_ = {};

  fbl::Mutex readers_lock_;
  std::list<std::unique_ptr<Ft8201InputReportsReader>> readers_list_ TA_GUARDED(readers_lock_);
  sync_completion_t next_reader_wait_;
  async::Loop loop_;
};

}  // namespace touch

#endif  // SRC_UI_INPUT_DRIVERS_FT8201_FT8201_H_
