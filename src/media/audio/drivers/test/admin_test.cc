// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/admin_test.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <algorithm>
#include <cstring>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

// static
bool AdminTest::device_access_denied_[2] = {false, false};

// Whenever the test group starts, try anew to get admin access (needed for "--gtest_repeat")
void AdminTest::SetUpTestSuite() {
  AdminTest::device_access_denied_[DeviceType::Input] = false;
  AdminTest::device_access_denied_[DeviceType::Output] = false;
}

// If earlier in this test group we failed to get admin access, exit immediately.
// If the admin flag is specified, then fail; otherwise, skip the test case.
void AdminTest::SetUp() {
  TestBase::SetUp();

  // If previous test in this group found no device, we don't need to search again - skip this test.
  if (no_devices_found()) {
    GTEST_SKIP();
  }

  if (device_access_denied()) {
    if (test_admin_functions_) {
      FAIL();
    } else {
      GTEST_SKIP();
    }
    __UNREACHABLE;
  }

  EnumerateDevices();
}

void AdminTest::TearDown() {
  ring_buffer_transceiver_.Close();

  TestBase::TearDown();
}

// For the channelization and sample_format that we've set, determine the size of each frame.
// This method assumes that the driver has already successfully responded to a SetFormat request.
void AdminTest::CalculateFrameSize() {
  if (error_occurred_ || !received_set_format_) {
    return;
  }

  switch (sample_format_) {
    case AUDIO_SAMPLE_FORMAT_8BIT:
      frame_size_ = num_channels_;
      break;

    case AUDIO_SAMPLE_FORMAT_16BIT:
      frame_size_ = num_channels_ * sizeof(int16_t);
      break;

    case AUDIO_SAMPLE_FORMAT_20BIT_PACKED:
      frame_size_ = (num_channels_ * 5 + 3) / 4;
      FX_LOGS(ERROR) << "AUDIO_SAMPLE_FORMAT_20BIT_PACKED is unvalidated in audio_core";
      break;

    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED:
      frame_size_ = num_channels_ * 3;
      break;

    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_32BIT:
      frame_size_ = num_channels_ * sizeof(int32_t);
      break;

    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:
      frame_size_ = num_channels_ * sizeof(float);
      break;

    case AUDIO_SAMPLE_FORMAT_BITSTREAM:
    default:
      ASSERT_TRUE(false) << "Unknown sample_format_ " << std::hex << sample_format_;
      frame_size_ = 0;
      break;
  }
}

void AdminTest::SelectFirstFormat() {
  if (received_get_formats()) {
    // strip off the UNSIGNED and INVERT_ENDIAN bits...
    auto first_range = format_ranges().front();
    auto first_format = first_range.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    ASSERT_NE(first_format, 0u);

    // just keep the lowest sample format bit.
    audio_sample_format_t bit = 1;
    while ((first_format & bit) == 0) {
      bit <<= 1;
    }
    first_format &= bit;

    frame_rate_ = first_range.min_frames_per_second;
    sample_format_ = first_format;
    num_channels_ = first_range.min_channels;
  }
}

void AdminTest::SelectLastFormat() {
  if (received_get_formats()) {
    // strip off the UNSIGNED and INVERT_ENDIAN bits...
    auto last_range = format_ranges().back();
    auto last_format = last_range.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    ASSERT_NE(last_format, 0u);

    // and just keep the highest remaining sample format bit.
    while (last_format & (last_format - 1)) {
      last_format &= (last_format - 1);
    }

    frame_rate_ = last_range.max_frames_per_second;
    sample_format_ = last_format;
    num_channels_ = last_range.max_channels;
  }
}

// Request that driver set format to the lowest rate/channelization of the first range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestSetFormatMin() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(format_ranges().size(), 0u);

  SelectFirstFormat();

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_format_req_t>();
  set_format_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_format_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;

  request.frames_per_second = frame_rate_;
  request.sample_format = sample_format_;
  request.channels = num_channels_;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() {
    return (received_set_format_ && ring_buffer_channel_ready_) || device_access_denied() ||
           error_occurred_;
  });
  CalculateFrameSize();
}

// Request that driver set format to the highest rate/channelization of the final range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestSetFormatMax() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(format_ranges().size(), 0u);

  SelectLastFormat();

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_format_req_t>();
  set_format_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_format_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;

  request.frames_per_second = frame_rate_;
  request.sample_format = sample_format_;
  request.channels = num_channels_;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() {
    return (received_set_format_ && ring_buffer_channel_ready_) || error_occurred_ ||
           device_access_denied();
  });
  CalculateFrameSize();
}

// Ring-buffer channel requests
//
// Request that the driver return the FIFO depth (in bytes), at the currently set format.
// This method relies on the ring buffer channel, received with response to a successful
// SetFormat.
void AdminTest::RequestFifoDepth() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(ring_buffer_channel_ready_);

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_get_fifo_depth_req_t>();
  get_fifo_depth_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_fifo_depth_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil(
      [this]() { return received_get_fifo_depth_ || error_occurred_ || device_access_denied(); });
}

// Request that the driver return a VMO handle for the ring buffer, at the currently set format.
// This method relies on the ring buffer channel, received with response to a successful
// SetFormat.
void AdminTest::RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring) {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(ring_buffer_channel_ready_);

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_get_buffer_req_t>();
  get_buffer_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_buffer_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;

  request.min_ring_buffer_frames = min_ring_buffer_frames;
  min_ring_buffer_frames_ = min_ring_buffer_frames;

  request.notifications_per_ring = notifications_per_ring;
  notifications_per_ring_ = notifications_per_ring;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil(
      [this]() { return received_get_buffer_ || error_occurred_ || device_access_denied(); });
}

// Request that the driver start the ring buffer engine, responding with the start_time.
// This method assumes that the ring buffer VMO was received in a successful GetBuffer response.
void AdminTest::RequestStart() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(ring_buffer_ready_);

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_start_req_t>();
  start_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = start_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_START;

  auto send_time = zx::clock::get_monotonic().get();
  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this, send_time]() {
    if (received_start_) {
      EXPECT_GT(start_time_, send_time);
    }
    return received_start_ || error_occurred_ || device_access_denied();
  });

  // TODO(mpuryear): validate start_time is neither too far in the future (it includes FIFO delay),
  // nor too far into the past (less than one ring-buffer of duration).
}

// Request that the driver stop the ring buffer engine, including quieting position notifications.
// This method assumes that the ring buffer engine has previously been successfully started.
void AdminTest::RequestStop() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(received_start_);

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_stop_req_t>();
  stop_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = stop_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_STOP;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_stop_ || error_occurred_ || device_access_denied(); });
}

void AdminTest::HandleInboundStreamMessage(
    media::audio::test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_STREAM_CMD_GET_UNIQUE_ID:
    case AUDIO_STREAM_CMD_GET_STRING:
    case AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN:
    case AUDIO_STREAM_CMD_GET_GAIN:
    case AUDIO_STREAM_CMD_SET_GAIN:
    case AUDIO_STREAM_CMD_PLUG_DETECT:
    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      EXPECT_TRUE(false) << "Unhandled basic command " << header.cmd << " during admin test";
      break;

    case AUDIO_STREAM_CMD_GET_FORMATS:
      HandleGetFormatsResponse(message.BytesAs<audio_stream_cmd_get_formats_resp_t>());
      break;

    case AUDIO_STREAM_CMD_SET_FORMAT:
      HandleSetFormatResponse(message.BytesAs<audio_stream_cmd_set_format_resp_t>());
      // On success, a channel used to control the audio buffer will be returned.
      ExtractRingBufferChannel(message);
      break;

    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

// Handle a set_format response on the stream channel (prepare to extract the ring buffer channel).
void AdminTest::HandleSetFormatResponse(const audio_stream_cmd_set_format_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, set_format_transaction_id_,
                              AUDIO_STREAM_CMD_SET_FORMAT)) {
    return;
  }

  if (response.result != ZX_OK) {
    if (response.result == ZX_ERR_ACCESS_DENIED) {
      FX_LOGS(WARNING) << "ZX_ERR_ACCESS_DENIED: Is audio_core already connected to the driver?";
      set_device_access_denied();
      if (!test_admin_functions_) {
        GTEST_SKIP();
      }
    }
    error_occurred_ = true;

    FAIL();
  }

  external_delay_nsec_ = response.external_delay_nsec;

  received_set_format_ = true;
}

// In concert with incoming SetFormat response on stream channel, extract the ring-buffer channel.
// With it, initialize the message transceiver that will handle messages to/from this channel.
void AdminTest::ExtractRingBufferChannel(media::audio::test::MessageTransceiver::Message message) {
  // if true, this is the first test to hit ACCESS_DENIED. We've already signaled to SKIP the test.
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  ASSERT_TRUE(received_set_format_);
  ASSERT_EQ(message.handles_.size(), 1u);

  EXPECT_EQ(ring_buffer_transceiver_.Init(
                zx::channel(message.handles_[0]),
                fit::bind_member(this, &AdminTest::OnInboundRingBufferMessage), ErrorHandler()),
            ZX_OK);
  message.handles_.clear();

  ring_buffer_channel_ready_ = true;
}

// Handle all incoming response message types, on the ring buffer channel.
void AdminTest::OnInboundRingBufferMessage(
    media::audio::test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_RB_CMD_GET_FIFO_DEPTH:
      HandleGetFifoDepthResponse(message.BytesAs<audio_rb_cmd_get_fifo_depth_resp_t>());
      break;

    case AUDIO_RB_CMD_GET_BUFFER:
      HandleGetBufferResponse(message.BytesAs<audio_rb_cmd_get_buffer_resp_t>());

      // On success, a VMO for the ring buffer will be returned.
      ExtractRingBuffer(message);
      break;

    case AUDIO_RB_CMD_START:
      HandleStartResponse(message.BytesAs<audio_rb_cmd_start_resp_t>());
      break;

    case AUDIO_RB_CMD_STOP:
      HandleStopResponse(message.BytesAs<audio_rb_cmd_stop_resp_t>());
      break;

    case AUDIO_RB_POSITION_NOTIFY:
      HandlePositionNotify(message.BytesAs<audio_rb_position_notify_t>());
      break;

    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

// Handle a get_fifo_depth response on the ring buffer channel.
void AdminTest::HandleGetFifoDepthResponse(const audio_rb_cmd_get_fifo_depth_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_fifo_depth_transaction_id_,
                              AUDIO_RB_CMD_GET_FIFO_DEPTH)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  fifo_depth_ = response.fifo_depth;

  received_get_fifo_depth_ = true;
}

// Handle a get_buffer response on the ring buffer channel.
void AdminTest::HandleGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_buffer_transaction_id_, AUDIO_RB_CMD_GET_BUFFER)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  EXPECT_GE(response.num_ring_buffer_frames, min_ring_buffer_frames_);
  ring_buffer_frames_ = response.num_ring_buffer_frames;

  received_get_buffer_ = true;
}

// Given the GET_BUFFER response message, retrieve the ring buffer VMO handle and save it.
void AdminTest::ExtractRingBuffer(
    media::audio::test::MessageTransceiver::Message get_buffer_response) {
  ASSERT_TRUE(received_get_buffer_);

  EXPECT_EQ(get_buffer_response.handles_.size(), 1u);
  zx::vmo ring_buffer_vmo = zx::vmo(get_buffer_response.handles_[0]);
  get_buffer_response.handles_.clear();
  EXPECT_TRUE(ring_buffer_vmo.is_valid());

  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  EXPECT_EQ(
      ring_buffer_.CreateAndMap(ring_buffer_frames_ * frame_size_, option_flags, nullptr,
                                &ring_buffer_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER),
      ZX_OK);

  AUD_VLOG(TRACE) << "Mapping size: " << ring_buffer_frames_ * frame_size_;

  ring_buffer_duration_ = ZX_SEC(1) * ring_buffer_frames_ / frame_rate_;
  ring_buffer_ready_ = true;
}

// Handle a start response on the ring buffer channel.
void AdminTest::HandleStartResponse(const audio_rb_cmd_start_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, start_transaction_id_, AUDIO_RB_CMD_START)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  ASSERT_GT(response.start_time, 0u);
  start_time_ = response.start_time;

  received_start_ = true;
}

// Handle a stop response on the ring buffer channel. Clear out any previous position notification
// count, to enable us to detect whether any were received after the STOP command was processed.
void AdminTest::HandleStopResponse(const audio_rb_cmd_stop_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, stop_transaction_id_, AUDIO_RB_CMD_STOP)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  position_notification_count_ = 0;
  received_stop_ = true;
}

// Handle a position notification on the ring buffer channel.
void AdminTest::HandlePositionNotify(const audio_rb_position_notify_t& notify) {
  if (!ValidateResponseHeader(notify.hdr, get_position_transaction_id_, AUDIO_RB_POSITION_NOTIFY)) {
    return;
  }

  EXPECT_GT(notifications_per_ring_, 0u);

  // Verify monotonic_time values: earlier than NOW, but less than a ring-buffer's duration in the
  // past; always increasing; cannot be earlier than start_time or later.
  auto now = zx::clock::get_monotonic().get();
  EXPECT_LT(start_time_, now);
  EXPECT_LT(notify.monotonic_time, now);
  EXPECT_GT(notify.monotonic_time, now - ring_buffer_duration_);

  if (position_notification_count_) {
    EXPECT_GT(notify.monotonic_time, start_time_);
    EXPECT_GT(notify.monotonic_time, last_monotonic_time_);
  } else {
    EXPECT_GE(notify.monotonic_time, start_time_);
  }

  // Verify ring_buffer_pos values: must be less than the ring-buffer size. Other possible checks:
  // - Is ring_buffer_pos an integer multiple of frame_size_?
  // - Are the right number of notifications delivered per ring-buffer cycle?
  last_monotonic_time_ = notify.monotonic_time;
  ring_buffer_position_ = notify.ring_buffer_pos;
  EXPECT_LT(ring_buffer_position_, ring_buffer_frames_ * frame_size_);

  ++position_notification_count_;

  AUD_VLOG(TRACE) << "Position: " << ring_buffer_position_
                  << ", notification_count: " << position_notification_count_;
}

// Wait for the specified number of position notifications, or timeout at 60 seconds.
void AdminTest::ExpectPositionNotifyCount(uint32_t count) {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  RunLoopUntil(
      [this, count]() { return position_notification_count_ >= count || error_occurred_; });

  auto timestamp_duration = last_monotonic_time_ - start_time_;
  auto observed_duration = zx::clock::get_monotonic().get() - start_time_;
  ASSERT_GE(position_notification_count_, count) << "No position notifications received";

  ASSERT_NE(frame_rate_ * notifications_per_ring_, 0u);
  auto ns_per_notification =
      (zx::sec(1) * ring_buffer_frames_) / (frame_rate_ * notifications_per_ring_);
  auto expected_min_time = ns_per_notification.get() * (count - 1);
  auto expected_time = ns_per_notification.get() * count;
  auto expected_max_time = ns_per_notification.get() * (count + 2);

  AUD_VLOG(TRACE) << "Timestamp delta from min/ideal/max: " << std::setw(10)
                  << (expected_min_time - timestamp_duration) << " : " << std::setw(10)
                  << (expected_time - timestamp_duration) << " : " << std::setw(10)
                  << (expected_max_time - timestamp_duration);
  EXPECT_GE(timestamp_duration, expected_min_time);
  EXPECT_LT(timestamp_duration, expected_max_time);

  AUD_VLOG(TRACE) << "Observed delta from min/ideal/max : " << std::setw(10)
                  << (expected_min_time - observed_duration) << " : " << std::setw(10)
                  << (expected_time - observed_duration) << " : " << std::setw(10)
                  << (expected_max_time - observed_duration);
  EXPECT_GT(observed_duration, expected_min_time);
}

// After waiting for one second, we should NOT have received any position notifications.
void AdminTest::ExpectNoPositionNotifications() {
  if (error_occurred_ || device_access_denied()) {
    return;
  }

  zx::nanosleep(zx::deadline_after(zx::sec(1)));
  RunLoopUntilIdle();

  EXPECT_EQ(position_notification_count_, 0u);
}

//
// Test cases that target each of the various admin commands
//
// Verify SET_FORMAT response (low-bit-rate) and that valid ring buffer channel is received.
TEST_P(AdminTest, SetFormatMin) {
  RequestFormats();
  RequestSetFormatMin();
}

// Verify SET_FORMAT response (high-bit-rate) and that valid ring buffer channel is received.
TEST_P(AdminTest, SetFormatMax) {
  RequestFormats();
  RequestSetFormatMax();
}

// Ring Buffer channel commands
//
// Verify a valid GET_FIFO_DEPTH response is successfully received.
TEST_P(AdminTest, GetFifoDepth) {
  RequestFormats();
  RequestSetFormatMax();
  RequestFifoDepth();
}

// Verify a GET_BUFFER response and ring buffer VMO is successfully received.
TEST_P(AdminTest, GetBuffer) {
  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(100u, 1u);
}

// Verify that a valid START response is successfully received.
TEST_P(AdminTest, Start) {
  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(32000, 0);
  RequestStart();
}

// Verify that a valid STOP response is successfully received.
TEST_P(AdminTest, Stop) {
  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(100, 0);
  RequestStart();
  RequestStop();
}

// Verify position notifications at fast rate (~180/sec) over approx 100 ms.
TEST_P(AdminTest, PositionNotifyFast) {
  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();
  ExpectPositionNotifyCount(16);
}

// Verify position notifications at slow rate (2/sec) over approx 1 second.
TEST_P(AdminTest, PositionNotifySlow) {
  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(48000, 2);
  RequestStart();
  ExpectPositionNotifyCount(2);
}

// Verify that no position notifications arrive if notifications_per_ring is 0.
TEST_P(AdminTest, PositionNotifyNone) {
  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 0);
  RequestStart();
  ExpectNoPositionNotifications();
}

// Verify that no position notificatons arrive after STOP.
TEST_P(AdminTest, NoPositionNotifyAfterStop) {
  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();
  ExpectPositionNotifyCount(2);
  RequestStop();
  ExpectNoPositionNotifications();
}

INSTANTIATE_TEST_SUITE_P(AudioDriverTests, AdminTest,
                         testing::Values(DeviceType::Input, DeviceType::Output),
                         TestBase::DeviceTypeToString);

}  // namespace media::audio::drivers::test
