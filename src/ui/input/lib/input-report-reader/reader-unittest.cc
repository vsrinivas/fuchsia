// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/input-report-reader/reader.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/server.h>

#include <zxtest/zxtest.h>

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

struct MouseReport {
  int64_t movement_x;
  int64_t movement_y;
  fuchsia_input_report::InputReport ToFidlInputReport(fidl::Allocator& allocator) {
    auto mouse = fuchsia_input_report::MouseInputReport::Builder(
        allocator.make<fuchsia_input_report::MouseInputReport::Frame>());
    mouse.set_movement_x(allocator.make<int64_t>(this->movement_x));
    mouse.set_movement_y(allocator.make<int64_t>(this->movement_y));

    auto time = allocator.make<zx_time_t>(0);

    return fuchsia_input_report::InputReport::Builder(
               allocator.make<fuchsia_input_report::InputReport::Frame>())
        .set_event_time(std::move(time))
        .set_mouse(allocator.make<fuchsia_input_report::MouseInputReport>(mouse.build()))
        .build();
  }
};

class MouseDevice : public fuchsia_input_report::InputDevice::Interface {
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
  void GetInputReportsReader(zx::channel server,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(fuchsia_input_report::OutputReport report,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(fuchsia_input_report::FeatureReport report,
                        SetFeatureReportCompleter::Sync& completer) override;

 private:
  sync_completion_t next_reader_wait_;
  input::InputReportReaderManager<MouseReport> input_report_readers_;
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

void MouseDevice::GetInputReportsReader(zx::channel server,
                                        GetInputReportsReaderCompleter::Sync& completer) {
  zx_status_t status = input_report_readers_.CreateReader(loop_.dispatcher(), std::move(server));
  if (status == ZX_OK) {
    // Signal to a test framework (if it exists) that we are connected to a reader.
    sync_completion_signal(&next_reader_wait_);
  }
}

void MouseDevice::GetDescriptor(GetDescriptorCompleter::Sync& completer) {
  constexpr size_t kDescriptorBufferSize = 512;
  fidl::BufferThenHeapAllocator<kDescriptorBufferSize> allocator;

  completer.Reply(fuchsia_input_report::DeviceDescriptor::Builder(
                      allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>())
                      .build());
}

void MouseDevice::SendOutputReport(fuchsia_input_report::OutputReport report,
                                   SendOutputReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void MouseDevice::GetFeatureReport(GetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void MouseDevice::SetFeatureReport(fuchsia_input_report::FeatureReport report,
                                   SetFeatureReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

class InputReportReaderTests : public zxtest::Test {
  void SetUp() override {
    ASSERT_EQ(mouse_.Start(), ZX_OK);
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    auto result = fidl::BindServer(loop_.dispatcher(), std::move(server), &mouse_);
    input_device_ = fuchsia_input_report::InputDevice::SyncClient(std::move(client));
    ASSERT_EQ(loop_.StartThread("MouseDeviceThread"), ZX_OK);
  }

  void TearDown() override {}

 protected:
  MouseDevice mouse_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
  fuchsia_input_report::InputDevice::SyncClient input_device_;
};

TEST_F(InputReportReaderTests, LifeTimeTest) {
  // Get an InputReportsReader.
  fuchsia_input_report::InputReportsReader::SyncClient reader;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    reader = fuchsia_input_report::InputReportsReader::SyncClient(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }
}

TEST_F(InputReportReaderTests, ReadInputReportsTest) {
  // Get an InputReportsReader.
  fuchsia_input_report::InputReportsReader::SyncClient reader;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    reader = fuchsia_input_report::InputReportsReader::SyncClient(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Send a report.
  MouseReport report;
  report.movement_x = 0x100;
  report.movement_y = 0x200;
  mouse_.SendReport(report);

  // Get the report.
  auto result = reader.ReadInputReports();
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  auto& reports = result->result.response().reports;

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

TEST_F(InputReportReaderTests, TwoReaders) {
  // Get the first reader.
  fuchsia_input_report::InputReportsReader::SyncClient reader_one;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    reader_one = fuchsia_input_report::InputReportsReader::SyncClient(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Get the second reader.
  fuchsia_input_report::InputReportsReader::SyncClient reader_two;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    reader_two = fuchsia_input_report::InputReportsReader::SyncClient(std::move(client));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Send a report.
  MouseReport report;
  report.movement_x = 0x100;
  report.movement_y = 0x200;
  mouse_.SendReport(report);

  // Get the first report.
  {
    auto result = reader_one.ReadInputReports();
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    auto& reports = result->result.response().reports;

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
    auto result = reader_two.ReadInputReports();
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
    auto& reports = result->result.response().reports;

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
  fidl::Client<llcpp::fuchsia::input::report::InputReportsReader> reader;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    ASSERT_OK(reader.Bind(std::move(client), loop.dispatcher()));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Read the report. This will hang until a report is sent.
  auto status = reader->ReadInputReports(
      [&](::llcpp::fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
        ASSERT_FALSE(result.is_err());
        auto& reports = result.response().reports;
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
  ASSERT_OK(status.status());
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
  fidl::Client<llcpp::fuchsia::input::report::InputReportsReader> reader;
  {
    zx::channel server, client;
    ASSERT_EQ(zx::channel::create(0, &server, &client), ZX_OK);
    input_device_.GetInputReportsReader(std::move(server));
    ASSERT_OK(reader.Bind(std::move(client), loop.dispatcher()));
    mouse_.WaitForNextReader(zx::duration::infinite());
  }

  // Queue a read.
  auto status = reader->ReadInputReports(
      [&](::llcpp::fuchsia::input::report::InputReportsReader_ReadInputReports_Result result) {
        ASSERT_TRUE(result.is_err());
      });
  ASSERT_OK(status.status());
  loop.RunUntilIdle();

  // Unbind the reader now that the report is waiting.
  reader.Unbind();
}
