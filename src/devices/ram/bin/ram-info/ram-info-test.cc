// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-info.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

namespace ram_metrics = fuchsia_hardware_ram_metrics;

namespace ram_info {
// fake register value used in test.
constexpr uint32_t TEST_REGISTER_VALUE = 42;

class FakeRamDevice : public fidl::WireServer<fuchsia_hardware_ram_metrics::Device> {
 public:
  FakeRamDevice() = default;

  void set_close() { completer_action_ = CompleterAction::kClose; }
  void set_reply_error() { completer_action_ = CompleterAction::kReplyError; }

  void GetDdrWindowingResults(GetDdrWindowingResultsRequestView request,
                              GetDdrWindowingResultsCompleter::Sync& completer) override {
    if (completer_action_ == CompleterAction::kClose) {
      completer.Close(0);
      return;
    }
    if (completer_action_ == CompleterAction::kReplyError) {
      completer.ReplyError(ZX_ERR_BAD_STATE);
      return;
    }
    completer.ReplySuccess(TEST_REGISTER_VALUE);
  }

  void MeasureBandwidth(MeasureBandwidthRequestView request,
                        MeasureBandwidthCompleter::Sync& completer) override {
    if (completer_action_ == CompleterAction::kClose) {
      completer.Close(0);
      return;
    }
    if (completer_action_ == CompleterAction::kReplyError) {
      completer.ReplyError(ZX_ERR_BAD_STATE);
      return;
    }

    EXPECT_EQ(request->config.cycles_to_measure % 1024, 0);
    auto mul = request->config.cycles_to_measure / 1024;

    ram_metrics::wire::BandwidthInfo info = {};
    info.timestamp = zx::msec(1234).to_nsecs();
    info.frequency = 256 * 1024 * 1024;
    info.bytes_per_cycle = 1;
    info.channels[0].readwrite_cycles = 10 * mul;
    info.channels[1].readwrite_cycles = 20 * mul;
    info.channels[2].readwrite_cycles = 30 * mul;
    info.channels[3].readwrite_cycles = 40 * mul;
    info.channels[4].readwrite_cycles = 50 * mul;
    info.channels[5].readwrite_cycles = 60 * mul;
    info.channels[6].readwrite_cycles = 70 * mul;
    info.channels[7].readwrite_cycles = 80 * mul;

    completer.ReplySuccess(info);
  }

 private:
  enum class CompleterAction {
    kClose,
    kReplyError,
    kReplySuccess,
  } completer_action_ = CompleterAction::kReplySuccess;
};

class RamInfoTest : public zxtest::Test {
 public:
  RamInfoTest() : zxtest::Test(), loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  void SetUp() override {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server), &fake_device_));
    loop_.StartThread("ram-info-test-loop");
  }

 protected:
  zx::channel client_;
  FakeRamDevice fake_device_;

 private:
  async::Loop loop_;
};

TEST_F(RamInfoTest, Errors) {
  constexpr uint64_t kCyclesToMeasure = 1024;

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  DefaultPrinter printer(output_file, kCyclesToMeasure);
  printer.AddChannelName(0, "channel0");

  fake_device_.set_close();
  EXPECT_NOT_OK(MeasureBandwith(&printer, std::move(client_), {}));

  fake_device_.set_reply_error();
  EXPECT_NOT_OK(MeasureBandwith(&printer, std::move(client_), {}));

  fclose(output_file);
}

TEST_F(RamInfoTest, DefaultPrinter) {
  constexpr uint64_t kCyclesToMeasure = 1024;

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  DefaultPrinter printer(output_file, kCyclesToMeasure);
  printer.AddChannelName(0, "channel0");
  printer.AddChannelName(1, "channel1");
  printer.AddChannelName(2, "channel2");
  printer.AddChannelName(3, "channel3");

  ram_metrics::wire::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = kCyclesToMeasure;

  EXPECT_OK(MeasureBandwith(&printer, std::move(client_), config));
  fclose(output_file);

  constexpr char kExpectedOutput[] =
      "channel \t\t usage (MB/s)  time: 1234 ms\n"
      "channel0 (rw) \t\t 2.5\n"
      "channel1 (rw) \t\t 5\n"
      "channel2 (rw) \t\t 7.5\n"
      "channel3 (rw) \t\t 10\n"
      "total (rw) \t\t 25\n";
  EXPECT_BYTES_EQ(output_buffer, kExpectedOutput, strlen(kExpectedOutput));
}

TEST_F(RamInfoTest, CsvPrinter) {
  constexpr uint64_t kCyclesToMeasure = 1024;

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  CsvPrinter printer(output_file, kCyclesToMeasure);
  printer.AddChannelName(0, "channel0");
  printer.AddChannelName(1, "channel1");
  printer.AddChannelName(2, "channel2");
  printer.AddChannelName(3, "channel3");

  ram_metrics::wire::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = kCyclesToMeasure;

  EXPECT_OK(MeasureBandwith(&printer, std::move(client_), config));
  fclose(output_file);

  constexpr char kExpectedOutput[] =
      "time,\"channel0\",\"channel1\",\"channel2\",\"channel3\"\n"
      "1234,2.5,5,7.5,10\n";
  EXPECT_BYTES_EQ(output_buffer, kExpectedOutput, strlen(kExpectedOutput));
}

TEST_F(RamInfoTest, ParseChannelString) {
  auto result = ParseChannelString("1234, 0x1234,  01234");
  ASSERT_TRUE(result.is_ok());

  std::array<uint64_t, 8> expected_values = {1234, 0x1234, 01234};
  for (size_t i = 0; i < expected_values.size(); i++) {
    EXPECT_EQ(result.value()[i], expected_values[i]);
  }

  result = ParseChannelString("0x10000000000000000");
  EXPECT_FALSE(result.is_ok());

  result = ParseChannelString("0xffffffffffffffff");
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.value()[0], 0xffff'ffff'ffff'ffff);

  result = ParseChannelString("1,2,3,4,5,6,7,8,");
  ASSERT_TRUE(result.is_ok());

  expected_values = {1, 2, 3, 4, 5, 6, 7, 8};
  for (size_t i = 0; i < expected_values.size(); i++) {
    EXPECT_EQ(result.value()[i], expected_values[i]);
  }

  result = ParseChannelString("1,2,3,4,5,6,7,8a");
  EXPECT_FALSE(result.is_ok());

  result = ParseChannelString("1,2,3,4,5,6,7,8,9");
  EXPECT_FALSE(result.is_ok());

  result = ParseChannelString("");
  EXPECT_FALSE(result.is_ok());

  result = ParseChannelString("z");
  EXPECT_FALSE(result.is_ok());
}

TEST_F(RamInfoTest, CyclesToMeasure) {
  constexpr uint64_t kCyclesToMeasure = 1024 * 20;

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  DefaultPrinter printer(output_file, kCyclesToMeasure);
  printer.AddChannelName(0, "channel0");
  printer.AddChannelName(1, "channel1");
  printer.AddChannelName(2, "channel2");
  printer.AddChannelName(3, "channel3");

  ram_metrics::wire::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = kCyclesToMeasure;

  EXPECT_OK(MeasureBandwith(&printer, std::move(client_), config));
  fclose(output_file);

  constexpr char kExpectedOutput[] =
      "channel \t\t usage (MB/s)  time: 1234 ms\n"
      "channel0 (rw) \t\t 2.5\n"
      "channel1 (rw) \t\t 5\n"
      "channel2 (rw) \t\t 7.5\n"
      "channel3 (rw) \t\t 10\n"
      "total (rw) \t\t 25\n";
  EXPECT_BYTES_EQ(output_buffer, kExpectedOutput, strlen(kExpectedOutput));
}

TEST_F(RamInfoTest, GetDdrWindowingResults) {
  // This test is pretty trivial, since we're faking the interface, and all the
  // function does is print the resulting value.
  EXPECT_OK(GetDdrWindowingResults(std::move(client_)));
}

}  // namespace ram_info
