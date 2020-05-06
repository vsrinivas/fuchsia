// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ram-info.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

namespace ram_metrics = ::llcpp::fuchsia::hardware::ram::metrics;

namespace ram_info {

class FakeRamDevice : public ::llcpp::fuchsia::hardware::ram::metrics::Device::Interface {
 public:
  FakeRamDevice() = default;

  void set_close() { completer_action_ = CompleterAction::kClose; }
  void set_reply_error() { completer_action_ = CompleterAction::kReplyError; }

  void MeasureBandwidth(ram_metrics::BandwidthMeasurementConfig config,
                        MeasureBandwidthCompleter::Sync completer) override {
    if (completer_action_ == CompleterAction::kClose) {
      completer.Close(0);
      return;
    }
    if (completer_action_ == CompleterAction::kReplyError) {
      completer.ReplyError(ZX_ERR_BAD_STATE);
      return;
    }

    EXPECT_EQ(config.cycles_to_measure, 1024);

    ram_metrics::BandwidthInfo info = {};
    info.timestamp = zx::msec(1234).to_nsecs();
    info.channels[0].readwrite_cycles = 10;
    info.channels[1].readwrite_cycles = 20;
    info.channels[2].readwrite_cycles = 30;
    info.channels[3].readwrite_cycles = 40;
    info.channels[4].readwrite_cycles = 50;
    info.channels[5].readwrite_cycles = 60;
    info.channels[6].readwrite_cycles = 70;
    info.channels[7].readwrite_cycles = 80;

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
    ASSERT_OK(fidl::Bind(loop_.dispatcher(), std::move(server), &fake_device_));
    loop_.StartThread("ram-info-test-loop");
  }

 protected:
  zx::channel client_;
  FakeRamDevice fake_device_;

 private:
  async::Loop loop_;
};

TEST_F(RamInfoTest, Errors) {
  constexpr RamDeviceInfo kDeviceInfo = {
      .counter_to_bandwidth_mbs = [](uint64_t counter) -> double {
        return static_cast<double>(counter);
      },
  };

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  DefaultPrinter printer(output_file, kDeviceInfo);
  printer.AddChannelName(0, "channel0");

  fake_device_.set_close();
  EXPECT_NOT_OK(MeasureBandwith(&printer, std::move(client_), {}));

  fake_device_.set_reply_error();
  EXPECT_NOT_OK(MeasureBandwith(&printer, std::move(client_), {}));

  fclose(output_file);
}

TEST_F(RamInfoTest, FourChannelsDefaultPrinter) {
  constexpr RamDeviceInfo kDeviceInfo = {
      .counter_to_bandwidth_mbs = [](uint64_t counter) -> double {
        return static_cast<double>(counter) / 4.0;
      },
  };

  char output_buffer[512];
  FILE* output_file = fmemopen(output_buffer, sizeof(output_buffer), "w");
  ASSERT_NOT_NULL(output_file);

  DefaultPrinter printer(output_file, kDeviceInfo);
  printer.AddChannelName(0, "channel0");
  printer.AddChannelName(1, "channel1");
  printer.AddChannelName(2, "channel2");
  printer.AddChannelName(3, "channel3");

  ram_metrics::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = 1024;

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

}  // namespace ram_info
