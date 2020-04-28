// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "src/media/audio/drivers/test/test_base.h"

namespace media::audio::drivers::test {

class BasicTest : public TestBase {
 protected:
  void SetUp() override;

  void RequestUniqueId();
  void RequestManufacturerString();
  void RequestProductString();
  void RequestClockDomain();

  void RequestGain();
  void RequestSetGain();
  void RequestSetGain(audio_set_gain_flags_t flags, float gain_db);

  void RequestPlugDetect();

  void HandleInboundStreamMessage(media::audio::test::MessageTransceiver::Message message) override;

  void HandleGetUniqueIdResponse(const audio_stream_cmd_get_unique_id_resp_t& response);
  void HandleGetStringResponse(const audio_stream_cmd_get_string_resp_t& response);
  void HandleGetClockDomainResponse(const audio_stream_cmd_get_clock_domain_resp_t& response);

  void HandleGetGainResponse(const audio_stream_cmd_get_gain_resp_t& response);
  void HandleSetGainResponse(const audio_stream_cmd_set_gain_resp_t& response);

  void HandlePlugDetect(audio_pd_notify_flags_t flags, zx_time_t plug_state_time);
  void HandlePlugDetectResponse(const audio_stream_cmd_plug_detect_resp_t& response);
  void HandlePlugDetectNotify(const audio_stream_cmd_plug_detect_resp_t& notify);

 private:
  zx_txid_t unique_id_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t manufacturer_string_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t product_string_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_clock_domain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  zx_txid_t get_gain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t set_gain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  zx_txid_t plug_detect_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  static constexpr size_t kUniqueIdLength = 16;
  std::array<uint8_t, kUniqueIdLength> unique_id_;
  std::string manufacturer_;
  std::string product_;

  // Except for sentinel value -1 (external clock domain), negative clock domain values are invalid
  static constexpr int32_t kExternalClockDomain = -1;
  static constexpr int32_t kInvalidClockDomain = -2;
  int32_t clock_domain_ = kInvalidClockDomain;

  bool cur_mute_ = false;
  bool can_mute_ = false;
  bool set_mute_ = false;

  bool cur_agc_ = false;
  bool can_agc_ = false;
  bool set_agc_ = false;

  float cur_gain_ = 0.0f;
  float min_gain_ = 0.0f;
  float max_gain_ = 0.0f;
  float gain_step_ = 0.0f;
  float set_gain_ = 0.0f;

  bool hardwired_ = false;
  bool should_plug_notify_ = false;
  bool can_plug_notify_ = false;
  bool plugged_ = false;
  zx_time_t plug_state_time_ = 0;

  bool received_get_unique_id_ = false;
  bool received_get_string_manufacturer_ = false;
  bool received_get_string_product_ = false;
  bool received_get_clock_domain_ = false;

  bool received_get_gain_ = false;
  bool received_set_gain_ = false;

  bool received_plug_detect_ = false;
  bool received_plug_detect_notify_ = false;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_BASIC_TEST_H_
