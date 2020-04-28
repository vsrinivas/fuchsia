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
// Request the driver's unique ID.
void BasicTest::RequestUniqueId() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_unique_id_req_t>();
  unique_id_transaction_id_ = NextTransactionId();

  request.hdr.transaction_id = unique_id_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_UNIQUE_ID;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  // Channel may disconnect; message may return an error: also check for error_occurred_
  RunLoopUntil([this]() { return received_get_unique_id_ || error_occurred_; });
}

// Request that the driver return its manufacturer string.
void BasicTest::RequestManufacturerString() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_string_req_t>();
  manufacturer_string_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = manufacturer_string_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_STRING;

  request.id = AUDIO_STREAM_STR_ID_MANUFACTURER;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_string_manufacturer_ || error_occurred_; });
}

// Request that the driver return its product string.
void BasicTest::RequestProductString() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_string_req_t>();
  product_string_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = product_string_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_STRING;

  request.id = AUDIO_STREAM_STR_ID_PRODUCT;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_string_product_ || error_occurred_; });
}

// Request that the driver return its clock domain.
void BasicTest::RequestClockDomain() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_clock_domain_req_t>();
  get_clock_domain_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_clock_domain_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_clock_domain_ || error_occurred_; });
}

// Request that the driver return its gain capabilities and current state.
void BasicTest::RequestGain() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_gain_req_t>();
  get_gain_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_gain_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_GAIN;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_gain_ || error_occurred_; });
}

// Determine an appropriate gain state to set, then call another method to make the SetGain request.
// This method assumes that the driver has successfully responded to a GetGain request.
void BasicTest::RequestSetGain() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_gain_);

  if (max_gain_ == min_gain_) {
    FX_LOGS(INFO) << "Fixed device gain: " << cur_gain_ << "dB";
    GTEST_SKIP();
  }

  set_gain_ = min_gain_;
  if (cur_gain_ == min_gain_) {
    set_gain_ += gain_step_;
  }

  // Toggle the mute if possible; toggle AGC if possible
  audio_set_gain_flags_t flags = AUDIO_SGF_GAIN_VALID;
  if (can_mute_) {
    flags |= AUDIO_SGF_MUTE_VALID;
    if (!cur_mute_) {
      flags |= AUDIO_SGF_MUTE;
    }
  }
  if (can_agc_) {
    flags |= AUDIO_SGF_AGC_VALID;
    if (!cur_agc_) {
      flags |= AUDIO_SGF_AGC;
    }
  }

  RequestSetGain(flags, set_gain_);
}

// Request that the driver set its gain state to the specified gain_db and flags.
// This method assumes that the driver has already successfully responded to a GetGain request.
void BasicTest::RequestSetGain(audio_set_gain_flags_t flags, float gain_db) {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_gain_);

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_gain_req_t>();
  set_gain_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_gain_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_GAIN;

  request.flags = flags;
  request.gain = gain_db;
  set_mute_ = (flags & AUDIO_SGF_MUTE_VALID) ? (flags & AUDIO_SGF_MUTE) : cur_mute_;
  set_agc_ = (flags & AUDIO_SGF_AGC_VALID) ? (flags & AUDIO_SGF_AGC) : cur_agc_;
  set_gain_ = (flags & AUDIO_SGF_GAIN_VALID) ? gain_db : cur_gain_;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_set_gain_ || error_occurred_; });
}

// Request that driver retrieve the current plug detection state and capabilities.
void BasicTest::RequestPlugDetect() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_plug_detect_req_t>();
  plug_detect_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = plug_detect_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;

  request.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;
  should_plug_notify_ = true;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_plug_detect_ || error_occurred_; });
}

void BasicTest::HandleInboundStreamMessage(
    media::audio::test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_STREAM_CMD_GET_UNIQUE_ID:
      HandleGetUniqueIdResponse(message.BytesAs<audio_stream_cmd_get_unique_id_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_STRING:
      HandleGetStringResponse(message.BytesAs<audio_stream_cmd_get_string_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN:
      HandleGetClockDomainResponse(message.BytesAs<audio_stream_cmd_get_clock_domain_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_GAIN:
      HandleGetGainResponse(message.BytesAs<audio_stream_cmd_get_gain_resp_t>());
      break;

    case AUDIO_STREAM_CMD_SET_GAIN:
      HandleSetGainResponse(message.BytesAs<audio_stream_cmd_set_gain_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_FORMATS:
      HandleGetFormatsResponse(message.BytesAs<audio_stream_cmd_get_formats_resp_t>());
      break;

    case AUDIO_STREAM_CMD_PLUG_DETECT:
      HandlePlugDetectResponse(message.BytesAs<audio_stream_cmd_plug_detect_resp_t>());
      break;

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      HandlePlugDetectNotify(message.BytesAs<audio_stream_cmd_plug_detect_resp_t>());
      break;

    case AUDIO_STREAM_CMD_SET_FORMAT:
      EXPECT_TRUE(false) << "Unhandled admin command " << header.cmd;
      break;

    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

// Handle a get_unique_id response on the stream channel.
// TODO(mpuryear): maintain a set of these and verify that within this test run, the IDs are unique.
void BasicTest::HandleGetUniqueIdResponse(const audio_stream_cmd_get_unique_id_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, unique_id_transaction_id_,
                              AUDIO_STREAM_CMD_GET_UNIQUE_ID)) {
    return;
  }

  EXPECT_EQ(sizeof(response.unique_id.data), sizeof(audio_stream_unique_id_t));
  memcpy(unique_id_.data(), response.unique_id.data, kUniqueIdLength);

  char id_buf[2 * kUniqueIdLength + 1];
  std::snprintf(id_buf, sizeof(id_buf),
                "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", unique_id_[0],
                unique_id_[1], unique_id_[2], unique_id_[3], unique_id_[4], unique_id_[5],
                unique_id_[6], unique_id_[7], unique_id_[8], unique_id_[9], unique_id_[10],
                unique_id_[11], unique_id_[12], unique_id_[13], unique_id_[14], unique_id_[15]);
  AUD_VLOG(TRACE) << "Received unique_id " << id_buf;

  received_get_unique_id_ = true;
}

// Handle a get_string response on the stream channel (either mfr or prod).
// Should we validate that these strings are non-empty?
void BasicTest::HandleGetStringResponse(const audio_stream_cmd_get_string_resp_t& response) {
  if (!ValidateResponseCommand(response.hdr, AUDIO_STREAM_CMD_GET_STRING)) {
    return;
  }

  constexpr auto kMaxStringLength =
      sizeof(audio_stream_cmd_get_string_resp_t) - sizeof(audio_cmd_hdr_t) - (3 * sizeof(uint32_t));
  EXPECT_LE(response.strlen, kMaxStringLength);
  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  auto response_str = reinterpret_cast<const char*>(response.str);
  if (response.id == AUDIO_STREAM_STR_ID_MANUFACTURER) {
    ValidateResponseTransaction(response.hdr, manufacturer_string_transaction_id_);

    manufacturer_ = std::string(response_str, response.strlen);
    received_get_string_manufacturer_ = true;
  } else if (response.id == AUDIO_STREAM_STR_ID_PRODUCT) {
    ValidateResponseTransaction(response.hdr, product_string_transaction_id_);

    product_ = std::string(response_str, response.strlen);
    received_get_string_product_ = true;
  } else {
    ASSERT_TRUE(false) << "Unrecognized string ID received: " << response.id;
  }
}

// Handle a get_clock_domain response on the stream channel.
void BasicTest::HandleGetClockDomainResponse(
    const audio_stream_cmd_get_clock_domain_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_clock_domain_transaction_id_,
                              AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN)) {
    return;
  }

  EXPECT_GE(response.clock_domain, kExternalClockDomain);
  clock_domain_ = response.clock_domain;

  received_get_clock_domain_ = true;
}

// Handle a get_gain response on the stream channel.
void BasicTest::HandleGetGainResponse(const audio_stream_cmd_get_gain_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_gain_transaction_id_, AUDIO_STREAM_CMD_GET_GAIN)) {
    return;
  }

  cur_mute_ = response.cur_mute;
  can_mute_ = response.can_mute;
  cur_agc_ = response.cur_agc;
  can_agc_ = response.can_agc;
  cur_gain_ = response.cur_gain;
  min_gain_ = response.min_gain;
  max_gain_ = response.max_gain;
  gain_step_ = response.gain_step;

  // if mute then the device must be capable of muting
  if (cur_mute_) {
    EXPECT_TRUE(can_mute_);
  }

  // if AGC enabled then the device must be capable of that
  if (cur_agc_) {
    EXPECT_TRUE(can_agc_);
  }

  // cur_gain must be within bounds.  gain_step cannot exceed the min-to-max gain range; it cannot
  // be negative and can only be zero if max_gain==min_gain
  EXPECT_GE(cur_gain_, min_gain_);
  EXPECT_LE(cur_gain_, max_gain_);
  if (max_gain_ > min_gain_) {
    EXPECT_GT(gain_step_, 0.0f);
    EXPECT_LE(gain_step_, max_gain_ - min_gain_);
  } else {
    EXPECT_EQ(gain_step_, 0.0f);
  }

  received_get_gain_ = true;
}

// Handle a set_gain response on the stream channel.
void BasicTest::HandleSetGainResponse(const audio_stream_cmd_set_gain_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, set_gain_transaction_id_, AUDIO_STREAM_CMD_SET_GAIN)) {
    return;
  }
  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  // mute must match the value we set, and if mute then the device must be capable of muting
  cur_mute_ = response.cur_mute;
  EXPECT_EQ(cur_mute_, set_mute_);
  if (cur_mute_) {
    EXPECT_TRUE(can_mute_);
  }

  // AGC must match the value we set, and if AGC enabled then the device must be capable of that
  cur_agc_ = response.cur_agc;
  EXPECT_EQ(cur_agc_, set_agc_);
  if (cur_agc_) {
    EXPECT_TRUE(can_agc_);
  }

  // gain must match the values we set
  cur_gain_ = response.cur_gain;
  EXPECT_EQ(cur_gain_, set_gain_);
  EXPECT_GE(cur_gain_, min_gain_);
  EXPECT_LE(cur_gain_, max_gain_);

  received_set_gain_ = true;
}

// Handle plug_detection on the stream channel (shared across response and notification).
void BasicTest::HandlePlugDetect(audio_pd_notify_flags_t flags, zx_time_t plug_state_time) {
  // If we received a plug-detection response earlier, does this match previously-reported caps?
  if (received_plug_detect_) {
    EXPECT_EQ(hardwired_, (flags & AUDIO_PDNF_HARDWIRED));
    EXPECT_EQ(can_plug_notify_, (flags & AUDIO_PDNF_CAN_NOTIFY));
  }

  hardwired_ = flags & AUDIO_PDNF_HARDWIRED;
  can_plug_notify_ = flags & AUDIO_PDNF_CAN_NOTIFY;
  plugged_ = flags & AUDIO_PDNF_PLUGGED;

  // plug_state_time must be in the past but later than the previously reported plug_state_time
  if (plug_state_time) {
    EXPECT_GT(plug_state_time, plug_state_time_);
  } else {
    EXPECT_GE(plug_state_time, plug_state_time_);
  }

  plug_state_time_ = plug_state_time;
  EXPECT_LT(plug_state_time_, zx::clock::get_monotonic().get());

  AUD_VLOG(TRACE) << "Plug_state_time: " << plug_state_time;
}

// Handle a plug_detect response on the stream channel (response solicited by client).
void BasicTest::HandlePlugDetectResponse(const audio_stream_cmd_plug_detect_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, plug_detect_transaction_id_,
                              AUDIO_STREAM_CMD_PLUG_DETECT)) {
    return;
  }

  HandlePlugDetect(response.flags, response.plug_state_time);
  received_plug_detect_ = true;
}

// Handle plug_detect notification on the stream channel (async message not solicited by client).
void BasicTest::HandlePlugDetectNotify(const audio_stream_cmd_plug_detect_resp_t& notify) {
  if (!ValidateResponseHeader(notify.hdr, AUDIO_INVALID_TRANSACTION_ID,
                              AUDIO_STREAM_PLUG_DETECT_NOTIFY)) {
    return;
  }

  EXPECT_FALSE(hardwired_);
  EXPECT_TRUE(can_plug_notify_);
  EXPECT_TRUE(should_plug_notify_);

  HandlePlugDetect(notify.flags, notify.plug_state_time);
  received_plug_detect_notify_ = true;

  AUD_LOG(ERROR) << "Driver generated an unsolicited asynchronous plug detect notification";
}

//
// Test cases that target each of the various Stream channel commands
//
// Verify a valid GET_UNIQUE_ID response is successfully received.
TEST_P(BasicTest, GetUniqueId) { RequestUniqueId(); }

// Verify a valid GET_STRING (MANUFACTURER) response is successfully received.
TEST_P(BasicTest, GetManufacturer) { RequestManufacturerString(); }

// Verify a valid GET_STRING (PRODUCT) response is successfully received.
TEST_P(BasicTest, GetProduct) { RequestProductString(); }

// Verify a valid GET_CLOCK_DOMAIN response is successfully received.
TEST_P(BasicTest, GetClockDomain) { RequestClockDomain(); }

// Verify a valid GET_GAIN response is successfully received.
TEST_P(BasicTest, GetGain) { RequestGain(); }

// Verify a valid SET_GAIN response is successfully received. GetGain is required first: it returns
// not only current gain but also gain capabilities (gain min/max/step, can_mute, can_agc).
TEST_P(BasicTest, SetGain) {
  RequestGain();
  RequestSetGain();
}

// Verify a valid GET_FORMATS response is successfully received.
TEST_P(BasicTest, GetFormats) { RequestFormats(); }

// Verify a valid PLUG_DETECT response is successfully received.
TEST_P(BasicTest, PlugDetect) { RequestPlugDetect(); }

// A driver's AUDIO_STREAM_PLUG_DETECT_NOTIFY function is not testable without a way to trigger the
// driver's internal hardware-detect mechanism, so that it emits unsolicited PLUG/UNPLUG events.
//

INSTANTIATE_TEST_SUITE_P(AudioDriverTests, BasicTest,
                         testing::Values(DeviceType::Input, DeviceType::Output),
                         TestBase::DeviceTypeToString);

}  // namespace media::audio::drivers::test
