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

  void ReadInputReports(ReadInputReportsCompleter::Sync& completer) TA_EXCL(&report_lock_) override;

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
using DeviceType = ddk::Device<Ft8201Device, ddk::Messageable, ddk::Unbindable>;

class Ft8201Device : public DeviceType,
                     fuchsia_input_report::InputDevice::Interface,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  Ft8201Device(zx_device_t* parent, ddk::I2cChannel i2c) : Ft8201Device(parent, i2c, {}, {}) {}

  Ft8201Device(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient interrupt_gpio,
               ddk::GpioProtocolClient reset_gpio)
      : DeviceType(parent),
        i2c_(i2c),
        interrupt_gpio_(interrupt_gpio),
        reset_gpio_(reset_gpio),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~Ft8201Device() override = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  // Visible for testing.
  static zx::status<Ft8201Device*> CreateAndGetDevice(void* ctx, zx_device_t* parent);

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

  void RemoveReaderFromList(Ft8201InputReportsReader* reader) TA_EXCL(readers_lock_);

  // Visible for testing.
  void WaitForNextReader();
  zx_status_t FirmwareDownloadIfNeeded();

 private:
  static constexpr uint8_t kPrambootPacketCommand = 0xae;
  static constexpr uint8_t kFirmwarePacketCommand = 0xbf;

  static Ft8201Contact ParseContact(const uint8_t* contact_buffer);
  static uint8_t CalculateEcc(const uint8_t* buffer, size_t size, uint8_t initial = 0);

  zx_status_t Init();

  // Enters romboot and returns true if firmware download is needed, returns false otherwise.
  zx::status<bool> CheckFirmwareAndStartRomboot(uint8_t firmware_version);

  // Waits for the specified boot ID value to be read. Sends a reset command between reads if
  // send_reset is true.
  zx_status_t WaitForBootId(uint16_t expected_id, zx::duration retry_sleep, bool send_reset);
  zx::status<uint16_t> GetBootId();

  // Returns true if the expected value was read before the timeout, false if not.
  zx::status<bool> WaitForFlashStatus(uint16_t expected_value, int tries, zx::duration retry_sleep);

  zx_status_t SendDataPacket(uint8_t command, uint32_t address, const uint8_t* buffer, size_t size);

  zx_status_t SendPramboot(const zx::vmo& vmo, size_t size);
  zx_status_t SendPrambootPacket(uint32_t address, const uint8_t* buffer, size_t size) {
    return SendDataPacket(kPrambootPacketCommand, address, buffer, size);
  }

  zx_status_t EraseFlash(size_t firmware_size);
  zx_status_t SendFirmware(const zx::vmo& vmo, size_t size);
  zx_status_t SendFirmwarePacket(uint32_t address, const uint8_t* buffer, size_t size) {
    return SendDataPacket(kFirmwarePacketCommand, address, buffer, size);
  }
  zx_status_t CheckFirmwareEcc(size_t size, uint8_t expected_ecc);

  zx::status<uint8_t> ReadReg8(uint8_t address);
  zx::status<uint16_t> ReadReg16(uint8_t address);

  zx_status_t Write8(uint8_t value);
  zx_status_t WriteReg8(uint8_t address, uint8_t value);

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
