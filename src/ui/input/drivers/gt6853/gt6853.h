// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_
#define SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <fidl/fuchsia.mem/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/input_report_reader/reader.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace touch {

constexpr uint32_t kMaxContacts = 10;

struct Gt6853Contact {
  uint32_t contact_id;
  int64_t position_x;
  int64_t position_y;
};

struct Gt6853InputReport {
  zx::time event_time;
  Gt6853Contact contacts[kMaxContacts];
  size_t num_contacts;

  void ToFidlInputReport(
      fidl::WireTableBuilder<fuchsia_input_report::wire::InputReport>& input_report,
      fidl::AnyArena& allocator);
};

class Gt6853Device;
using DeviceType =
    ddk::Device<Gt6853Device, ddk::Messageable<fuchsia_input_report::InputDevice>::Mixin,
                ddk::Unbindable>;

class Gt6853Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  enum class Register : uint16_t {
    kDspMcuPower = 0x2010,
    kBankSelect = 0x2048,
    kCache = 0x204b,
    kAccessPatch0 = 0x204d,
    kWtdTimer = 0x20b0,
    kCpuCtrl = 0x2180,
    kScramble = 0x2218,
    kEsdKey = 0x2318,
    kEventStatusReg = 0x4100,
    kContactsReg = 0x4101,
    kContactsStartReg = 0x4102,
    kCpuRunFrom = 0x4506,
    kSensorIdReg = 0x4541,
    kIspRunFlag = 0x6006,
    kSubsysType = 0x6020,
    kFlashFlag = 0x6022,
    kCommandReg = 0x60cc,
    kConfigDataReg = 0x60dc,
    kIspBuffer = 0x6100,
    kIspAddr = 0xc000,
  };

  Gt6853Device(zx_device_t* parent, ddk::I2cChannel i2c)
      : Gt6853Device(parent, std::move(i2c), {}, {}) {}

  Gt6853Device(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient interrupt_gpio,
               ddk::GpioProtocolClient reset_gpio)
      : DeviceType(parent),
        i2c_(std::move(i2c)),
        interrupt_gpio_(interrupt_gpio),
        reset_gpio_(reset_gpio),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ~Gt6853Device() override = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  static bool RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel);

  void DdkRelease() { delete this; }

  void DdkUnbind(ddk::UnbindTxn txn);

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override;
  void GetInputReport(GetInputReportRequestView request,
                      GetInputReportCompleter::Sync& completer) override;

  // Visible for testing.
  void WaitForNextReader();

 private:
  enum class HostCommand : uint8_t;
  enum class DeviceCommand : uint8_t;

  struct FirmwareSubsysInfo {
    uint8_t type;
    uint32_t size;
    uint16_t flash_addr;
    const uint8_t* data;
  };

  static Gt6853Contact ParseContact(const uint8_t* contact_buffer);

  // To be implemented in a device-specific file. Should set panel_type_id_ and panel_type_, and
  // config_status_ in cases when the config download is skipped. Returns an invalid VMO if the
  // firmware update and config download should be skipped.
  zx::result<fuchsia_mem::wire::Range> GetConfigFileVmo();

  zx_status_t Init();

  zx_status_t DownloadConfigIfNeeded(const fuchsia_mem::wire::Range& config_file);
  static zx::result<uint64_t> GetConfigOffset(const fzl::VmoMapper& mapped_config,
                                              uint8_t sensor_id);
  zx_status_t PollCommandRegister(DeviceCommand command);
  zx_status_t SendCommand(HostCommand command);
  zx_status_t SendConfig(cpp20::span<const uint8_t> config);

  zx_status_t UpdateFirmwareIfNeeded();
  // Returns the number of subsys entries found and populated.
  static zx::result<size_t> ParseFirmwareInfo(const fzl::VmoMapper& mapped_fw,
                                              FirmwareSubsysInfo* out_subsys_entries);
  zx_status_t PrepareFirmwareUpdate(cpp20::span<const FirmwareSubsysInfo> subsys_entries);
  zx_status_t LoadIsp(const FirmwareSubsysInfo& isp_info);
  zx_status_t FlashSubsystem(const FirmwareSubsysInfo& subsys_info);
  static uint16_t Checksum16(const uint8_t* data, size_t size);
  zx_status_t SendFirmwarePacket(uint8_t type, const uint8_t* packet, size_t size);
  zx_status_t FinishFirmwareUpdate();

  zx::result<uint8_t> ReadReg8(Register reg);
  zx::result<> Read(Register reg, uint8_t* buffer, size_t size);
  zx::result<> WriteReg8(Register reg, uint8_t value);
  zx::result<> Write(Register reg, const uint8_t* buffer, size_t size);
  zx::result<> WriteAndCheck(Register reg, const uint8_t* buffer, size_t size);

  int Thread();
  void Shutdown();  // Only called after thread_ has been started.

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient interrupt_gpio_;
  ddk::GpioProtocolClient reset_gpio_;
  zx::interrupt interrupt_;

  thrd_t thread_ = {};

  input_report_reader::InputReportReaderManager<Gt6853InputReport> input_report_readers_;
  sync_completion_t next_reader_wait_;
  async::Loop loop_;

  inspect::Inspector inspector_;
  inspect::Node root_;

  inspect::IntProperty sensor_id_;
  inspect::IntProperty panel_type_id_;
  inspect::StringProperty panel_type_;
  inspect::StringProperty firmware_status_;
  inspect::StringProperty config_status_;

  inspect::Node metrics_root_;
  inspect::UintProperty average_latency_usecs_;
  inspect::UintProperty max_latency_usecs_;

  uint64_t report_count_ = 0;
  zx::duration total_latency_ = {};
  zx::duration max_latency_ = {};
};

}  // namespace touch

#endif  // SRC_UI_INPUT_DRIVERS_GT6853_GT6853_H_
