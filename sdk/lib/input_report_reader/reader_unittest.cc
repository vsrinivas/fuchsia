// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/input_report_reader/reader.h>

#include <zxtest/zxtest.h>

struct MouseReport {
  int64_t movement_x;
  int64_t movement_y;
  void ToFidlInputReport(
      fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>& input_report,
      fidl::AnyArena& allocator) {
    auto mouse = fuchsia_input_report::wire::MouseInputReport::Builder(allocator);
    mouse.movement_x(this->movement_x);
    mouse.movement_y(this->movement_y);

    input_report.mouse(mouse.Build());
  }
};

class MouseDevice : public fidl::WireServer<fuchsia_input_report::InputDevice> {
 public:
  zx_status_t Start();

  void SendReport(const MouseReport& report);
  // Function for testing that blocks until a new reader is connected.
  zx_status_t WaitForNextReader(zx::duration timeout) {
    zx_status_t status = sync_completion_wait(&next_reader_wait_, timeout.get());
    if (status == ZX_OK) {
      sync_completion_reset(&next_reader_wait_);
    }
    return ZX_OK;
  }

  // The FIDL methods for InputDevice.
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

 private:
  sync_completion_t next_reader_wait_;
  input_report_reader::InputReportReaderManager<MouseReport> input_report_readers_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
};

zx_status_t MouseDevice::Start() {
  zx_status_t status = ZX_OK;
  if ((status = loop_.StartThread("MouseDeviceReaderThread")) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

void MouseDevice::SendReport(const MouseReport& report) {
  input_report_readers_.SendReportToAllReaders(report);
}

void MouseDevice::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                        GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status =
      input_report_readers_.CreateReader(loop_.dispatcher(), std::move(request->reader));
  if (status == ZX_OK) {
    // Signal to a test framework (if it exists) that we are connected to a reader.
    sync_completion_signal(&next_reader_wait_);
  }
}

void MouseDevice::GetDescriptor(GetDescriptorRequestView request,
                                GetDescriptorCompleter::Sync& completer) {
  fidl::Arena allocator;

  completer.Reply(fuchsia_input_report::wire::DeviceDescriptor::Builder(allocator).Build());
}

void MouseDevice::SendOutputReport(SendOutputReportRequestView request,
                                   SendOutputReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void MouseDevice::GetFeatureReport(GetFeatureReportRequestView request,
                                   GetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void MouseDevice::SetFeatureReport(SetFeatureReportRequestView request,
                                   SetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void MouseDevice::GetInputReport(GetInputReportRequestView request,
                                 GetInputReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

class InputReportReaderTests : public zxtest::Test {
  void SetUp() override {
    ASSERT_EQ(mouse_.Start(), ZX_OK);
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputDevice>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    auto result = fidl::BindServer(loop_.dispatcher(), std::move(server), &mouse_);
    input_device_ = fidl::WireSyncClient<fuchsia_input_report::InputDevice>(std::move(client));
    ASSERT_EQ(loop_.StartThread("MouseDeviceThread"), ZX_OK);
  }

  void TearDown() override {}

 protected:
  MouseDevice mouse_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> input_device_;
};

TEST_F(InputReportReaderTests, LifeTimeTest) {
  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }
}

TEST_F(InputReportReaderTests, ReadInputReportsTest) {
  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Send a report.
  MouseReport report;
  report.movement_x = 0x100;
  report.movement_y = 0x200;
  mouse_.SendReport(report);

  // Get the report.
  auto result = reader->ReadInputReports();
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->is_error());
  auto& reports = result->value()->reports;

  ASSERT_EQ(1, reports.count());

  ASSERT_TRUE(reports[0].has_event_time());
  ASSERT_TRUE(reports[0].has_mouse());
  auto& mouse_report = reports[0].mouse();

  ASSERT_TRUE(mouse_report.has_movement_x());
  ASSERT_EQ(0x100, mouse_report.movement_x());

  ASSERT_TRUE(mouse_report.has_movement_y());
  ASSERT_EQ(0x200, mouse_report.movement_y());

  ASSERT_FALSE(mouse_report.has_pressed_buttons());
}

TEST_F(InputReportReaderTests, ReaderAddsRequiredFields) {
  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Send a report.
  MouseReport report;
  report.movement_x = 0x100;
  report.movement_y = 0x200;
  mouse_.SendReport(report);

  // Get the report.
  auto result = reader->ReadInputReports();
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->is_error());
  auto& reports = result->value()->reports;

  ASSERT_EQ(1, reports.count());

  ASSERT_TRUE(reports[0].has_event_time());
  ASSERT_TRUE(reports[0].has_trace_id());
}

TEST_F(InputReportReaderTests, TwoReaders) {
  // Get the first reader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader_one;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader_one = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Get the second reader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader_two;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader_two = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Send a report.
  MouseReport report;
  report.movement_x = 0x100;
  report.movement_y = 0x200;
  mouse_.SendReport(report);

  // Get the first report.
  {
    auto result = reader_one->ReadInputReports();
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->is_error());
    auto& reports = result->value()->reports;

    ASSERT_EQ(1, reports.count());

    ASSERT_TRUE(reports[0].has_event_time());
    ASSERT_TRUE(reports[0].has_mouse());
    auto& mouse_report = reports[0].mouse();

    ASSERT_TRUE(mouse_report.has_movement_x());
    ASSERT_EQ(0x100, mouse_report.movement_x());

    ASSERT_TRUE(mouse_report.has_movement_y());
    ASSERT_EQ(0x200, mouse_report.movement_y());

    ASSERT_FALSE(mouse_report.has_pressed_buttons());
  }

  // Get the second report.
  {
    auto result = reader_two->ReadInputReports();
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->is_error());
    auto& reports = result->value()->reports;

    ASSERT_EQ(1, reports.count());

    ASSERT_TRUE(reports[0].has_event_time());
    ASSERT_TRUE(reports[0].has_mouse());
    auto& mouse_report = reports[0].mouse();

    ASSERT_TRUE(mouse_report.has_movement_x());
    ASSERT_EQ(0x100, mouse_report.movement_x());

    ASSERT_TRUE(mouse_report.has_movement_y());
    ASSERT_EQ(0x200, mouse_report.movement_y());

    ASSERT_FALSE(mouse_report.has_pressed_buttons());
  }
}

TEST_F(InputReportReaderTests, ReadInputReportsHangingGetTest) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  // Get an async InputReportsReader.
  fidl::WireClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader.Bind(std::move(client), loop.dispatcher());
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Read the report. This will hang until a report is sent.
  reader->ReadInputReports().ThenExactlyOnce(
      [&](fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
              result) {
        ASSERT_OK(result.status());
        ASSERT_FALSE(result->is_error());
        auto& reports = result->value()->reports;
        ASSERT_EQ(1, reports.count());

        auto& report = reports[0];
        ASSERT_TRUE(report.has_event_time());
        ASSERT_TRUE(report.has_mouse());
        auto& mouse = report.mouse();

        ASSERT_TRUE(mouse.has_movement_x());
        ASSERT_EQ(0x50, mouse.movement_x());

        ASSERT_TRUE(mouse.has_movement_y());
        ASSERT_EQ(0x70, mouse.movement_y());
        loop.Quit();
      });
  loop.RunUntilIdle();

  // Send the report.
  MouseReport report;
  report.movement_x = 0x50;
  report.movement_y = 0x70;
  mouse_.SendReport(report);

  loop.Run();
}

TEST_F(InputReportReaderTests, CloseReaderWithOutstandingRead) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  // Get an async InputReportsReader.
  fidl::WireClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client, server] = std::move(endpoints.value());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)input_device_->GetInputReportsReader(std::move(server));
    reader.Bind(std::move(client), loop.dispatcher());
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Queue a read.
  reader->ReadInputReports().ThenExactlyOnce(
      [&](fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
              result) { ASSERT_TRUE(result.is_canceled()); });

  loop.RunUntilIdle();

  // Unbind the reader now that the report is waiting.
  reader = {};
}
