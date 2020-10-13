// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/basic_test.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include <algorithm>
#include <cstring>

#include "src/media/audio/lib/logging/logging.h"
#include "src/media/audio/lib/test/message_transceiver.h"

namespace media::audio::drivers::test {

void BasicTest::SetUp() {
  TestBase::SetUp();

  // If previous test in this group found no device, we don't need to search again - skip this test.
  if (no_devices_found()) {
    GTEST_SKIP();
  }

  EnumerateDevices();
}

// Stream channel requests
//
// Request the stream properties including the driver's unique ID which must be unique between input
// and output.
// TODO(mpuryear): ensure that this differs between input and output.
void BasicTest::RequestStreamProperties() {
  stream_config()->GetProperties([this](fuchsia::hardware::audio::StreamProperties prop) {
    stream_props_ = std::move(prop);

    if (stream_props_.has_unique_id()) {
      char id_buf[2 * kUniqueIdLength + 1];
      std::snprintf(id_buf, sizeof(id_buf),
                    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                    stream_props_.unique_id()[0], stream_props_.unique_id()[1],
                    stream_props_.unique_id()[2], stream_props_.unique_id()[3],
                    stream_props_.unique_id()[4], stream_props_.unique_id()[5],
                    stream_props_.unique_id()[6], stream_props_.unique_id()[7],
                    stream_props_.unique_id()[8], stream_props_.unique_id()[9],
                    stream_props_.unique_id()[10], stream_props_.unique_id()[11],
                    stream_props_.unique_id()[12], stream_props_.unique_id()[13],
                    stream_props_.unique_id()[14], stream_props_.unique_id()[15]);
      AUDIO_LOG(DEBUG) << "Received unique_id " << id_buf;
    }

    ASSERT_TRUE(stream_props_.has_is_input());
    if (device_type() == DeviceType::Input) {
      ASSERT_TRUE(prop.is_input());
    } else {
      ASSERT_FALSE(prop.is_input());
    }

    if (stream_props_.has_can_mute()) {
      *stream_props_.mutable_can_mute() = false;
    }
    if (stream_props_.has_can_agc()) {
      *stream_props_.mutable_can_agc() = false;
    }

    ASSERT_TRUE(stream_props_.has_min_gain_db());
    ASSERT_TRUE(stream_props_.has_max_gain_db());
    ASSERT_TRUE(stream_props_.has_gain_step_db());
    ASSERT_TRUE(stream_props_.min_gain_db() <= stream_props_.max_gain_db());
    ASSERT_TRUE(stream_props_.gain_step_db() >= 0);
    if (stream_props_.max_gain_db() > stream_props_.min_gain_db()) {
      EXPECT_GE(stream_props_.gain_step_db(), 0.0f);
    } else {
      EXPECT_EQ(stream_props_.gain_step_db(), 0.0f);
    }

    ASSERT_TRUE(stream_props_.has_plug_detect_capabilities());

    if (stream_props_.has_manufacturer()) {
      AUDIO_LOG(DEBUG) << "Received manufacturer " << stream_props_.manufacturer();
    }
    if (stream_props_.has_product()) {
      AUDIO_LOG(DEBUG) << "Received product " << stream_props_.product();
    }

    ASSERT_TRUE(stream_props_.has_clock_domain());

    received_get_stream_properties_ = true;
  });
  RunLoopUntil([this]() { return received_get_stream_properties_; });
}

// Request that the driver return its gain capabilities and current state.
void BasicTest::RequestGain() {
  ASSERT_FALSE(issued_set_gain_);  // Must request gain capabilities before seeting gain.
  // Since we reconnect to the audio stream every time we run this test and we are guaranteed by the
  // audio driver interface definition that the driver will reply to the first watch request, we
  // can get the gain state by issuing a watch FIDL call.
  stream_config()->WatchGainState([this](fuchsia::hardware::audio::GainState gain_state) {
    AUDIO_LOG(DEBUG) << "Received gain " << gain_state.gain_db();

    gain_state_ = std::move(gain_state);

    if (!gain_state_.has_muted()) {
      *gain_state_.mutable_muted() = false;
    }
    if (!gain_state_.has_agc_enabled()) {
      *gain_state_.mutable_agc_enabled() = false;
    }
    EXPECT_TRUE(gain_state_.has_gain_db());

    if (gain_state_.muted()) {
      EXPECT_TRUE(stream_props_.can_mute());
    }
    if (gain_state_.agc_enabled()) {
      EXPECT_TRUE(stream_props_.can_agc());
    }
    EXPECT_GE(gain_state_.gain_db(), stream_props_.min_gain_db());
    EXPECT_LE(gain_state_.gain_db(), stream_props_.max_gain_db());

    // We require that audio drivers have a default gain no greater than 0dB.
    EXPECT_LE(gain_state_.gain_db(), 0.f);

    received_get_gain_ = true;
  });
  RunLoopUntil([this]() { return received_get_gain_; });
}

// Determine an appropriate gain state to request, then call other method to request to the
// driver. This method assumes that the driver has already successfully responded to a GetGain
// request.
void BasicTest::RequestSetGain() {
  ASSERT_TRUE(received_get_gain_);

  if (stream_props_.max_gain_db() == stream_props_.min_gain_db()) {
    FX_LOGS(WARNING) << "*** Audio " << ((device_type() == DeviceType::Input) ? "input" : "output")
                     << " has fixed gain (" << gain_state_.gain_db()
                     << " dB). Skipping SetGain test. ***";
    return;
  }

  EXPECT_EQ(gain_state_.Clone(&set_gain_state_), ZX_OK);
  *set_gain_state_.mutable_gain_db() = stream_props_.min_gain_db();
  if (gain_state_.gain_db() == stream_props_.min_gain_db()) {
    *set_gain_state_.mutable_gain_db() += stream_props_.gain_step_db();
  }

  fuchsia::hardware::audio::GainState gain_state;
  EXPECT_EQ(set_gain_state_.Clone(&gain_state), ZX_OK);
  AUDIO_LOG(DEBUG) << "Sent gain " << gain_state.gain_db();
  stream_config()->SetGain(std::move(gain_state));
  issued_set_gain_ = true;
}

// Request that driver retrieve the current plug detection state.
void BasicTest::RequestPlugDetect() {
  // Since we reconnect to the audio stream every time we run this test and we are guaranteed by the
  // audio driver interface definition that the driver will reply to the first watch request, we
  // can get the plug state by issuing a watch FIDL call.
  stream_config()->WatchPlugState([this](fuchsia::hardware::audio::PlugState state) {
    plug_state_ = std::move(state);

    EXPECT_TRUE(plug_state_.has_plugged());
    EXPECT_TRUE(plug_state_.has_plug_state_time());
    EXPECT_LT(plug_state_.plug_state_time(), zx::clock::get_monotonic().get());

    AUDIO_LOG(DEBUG) << "Plug_state_time: " << plug_state_.plug_state_time();
    received_plug_detect_ = true;
  });
  RunLoopUntil([this]() { return received_plug_detect_; });
}

//
// Test cases that target each of the various Stream channel commands
//
// Verify a valid unique_id, manufacturer, product and gain capabilites is successfully received.
TEST_P(BasicTest, StreamProperties) { RequestStreamProperties(); }

// Verify a valid get gain response is successfully received.
TEST_P(BasicTest, GetGain) {
  RequestStreamProperties();
  RequestGain();
}

// Verify a valid set gain response is successfully received. GetGain is required first: it returns
// not only current gain but also gain capabilities (gain min/max/step, can_mute, can_agc).
TEST_P(BasicTest, SetGain) {
  RequestStreamProperties();
  RequestGain();
  RequestSetGain();
}

// Verify a valid get formats response is successfully received.
TEST_P(BasicTest, GetFormats) {
  RequestStreamProperties();
  RequestFormats();
}

// Verify a valid plug detect response is successfully received.
TEST_P(BasicTest, PlugDetect) {
  RequestStreamProperties();
  RequestPlugDetect();
}

// A driver's plug detect updates are not testable without a way to trigger the
// driver's internal hardware-detect mechanism, so that it emits unsolicited PLUG/UNPLUG events.
//

INSTANTIATE_TEST_SUITE_P(AudioDriverTests, BasicTest,
                         testing::Values(DeviceType::Input, DeviceType::Output),
                         TestBase::DeviceTypeToString);

}  // namespace media::audio::drivers::test
