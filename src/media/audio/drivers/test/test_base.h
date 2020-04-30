// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <zircon/device/audio.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "src/media/audio/lib/test/message_transceiver.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::drivers::test {

enum DeviceType : uint16_t { Input = 0, Output = 1 };

class TestBase : public media::audio::test::TestFixture,
                 public testing::WithParamInterface<DeviceType> {
 public:
  static bool test_admin_functions_;
  static void SetUpTestSuite();
  static std::string DeviceTypeToString(const testing::TestParamInfo<TestBase::ParamType>& info);

 protected:
  void SetUp() override;
  void TearDown() override;

  void EnumerateDevices();
  void AddDevice(int dir_fd, const std::string& name);

  void set_device_type(DeviceType device_type) { device_type_ = device_type; }
  DeviceType device_type() const { return device_type_; }
  bool no_devices_found() const { return no_devices_found_[device_type()]; }

  // "Basic" (stream-config channel) tests and "Admin" (ring-buffer channel) tests both need to know
  // the supported formats, so this is implemented in the shared base class.
  void RequestFormats();
  void HandleGetFormatsResponse(const audio_stream_cmd_get_formats_resp_t& response);

  bool received_get_formats() const { return received_get_formats_; }
  const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& pcm_formats() const {
    return pcm_formats_;
  }
  const fidl::InterfacePtr<fuchsia::hardware::audio::StreamConfig>& stream_config() {
    return stream_config_;
  }

 private:
  // for DeviceType::Input and DeviceType::Output
  static bool no_devices_found_[2];
  DeviceType device_type_;
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;

  bool stream_config_ready_ = false;
  fidl::InterfacePtr<fuchsia::hardware::audio::StreamConfig> stream_config_;

  std::vector<zx::unowned_channel> stream_channels_;

  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> pcm_formats_;
  bool received_get_formats_ = false;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_
