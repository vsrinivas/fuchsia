// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/audio.h>

#include "src/media/audio/drivers/test/audio_device_enumerator_stub.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::drivers::test {

enum DeviceType : uint16_t { Input = 0, Output = 1 };

struct DeviceEntry {
  int dir_fd;
  std::string filename;
  DeviceType dev_type;

  // File descriptors only use the non-negative range, leaving room for special values such as A2DP.
  static constexpr int kA2dp = -1;

  bool operator<(const DeviceEntry& rhs) const {
    return std::tie(dir_fd, filename, dev_type) < std::tie(rhs.dir_fd, rhs.filename, rhs.dev_type);
  }
};

// Used in registering separate test case instances for each enumerated device
//
// See googletest/docs/advanced.md for details
//
// Devices are displayed in the 'audio-output-2/000' format, or simply the filename, if the
// special dir_fd value is observed (an example might be 'Bluetooth-A2DP' for Bluetooth devices).
std::string inline DevNameForEntry(const DeviceEntry& device_entry) {
  if (device_entry.dir_fd == DeviceEntry::kA2dp) {
    return device_entry.filename;
  }

  return std::string(device_entry.dev_type == DeviceType::Input ? "audio-input-2"
                                                                : "audio-output-2") +
         "/" + device_entry.filename;
}
std::string inline TestNameForEntry(const std::string& test_class_name,
                                    const DeviceEntry& device_entry) {
  return DevNameForEntry(device_entry) + ":" + test_class_name;
}

class TestBase : public media::audio::test::TestFixture {
 public:
  explicit TestBase(const DeviceEntry& device_entry) : device_entry_(device_entry) {}

 protected:
  void SetUp() override;
  void TearDown() override;

  void ConnectToDevice(const DeviceEntry& device_entry);
  void ConnectToBluetoothDevice();
  void CreateStreamConfigFromChannel(
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel);

  const DeviceEntry& device_entry() const { return device_entry_; }
  void set_device_type(DeviceType device_type) {}
  DeviceType device_type() const { return device_entry_.dev_type; }

  // "Basic" (stream-config channel) tests and "Admin" (ring-buffer channel) tests both need to know
  // the supported formats, so this is implemented in the shared base class.
  void RequestFormats();

  bool received_get_formats() const { return received_get_formats_; }
  const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& pcm_formats() const {
    return pcm_formats_;
  }

  fidl::InterfacePtr<fuchsia::hardware::audio::StreamConfig>& stream_config() {
    return stream_config_;
  }

  void set_failed() { failed_ = true; }
  bool failed() const { return failed_; }

  void WaitForError(zx::duration wait_duration = kWaitForErrorDuration) {
    RunLoopWithTimeoutOrUntil([this]() { return failed(); }, wait_duration);
  }

 private:
  static constexpr zx::duration kWaitForErrorDuration = zx::msec(100);

  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  fuchsia::sys::ComponentControllerPtr bt_harness_;

  bool failed_ = false;
  const DeviceEntry& device_entry_;

  fidl::InterfacePtr<fuchsia::hardware::audio::StreamConfig> stream_config_;

  bool received_get_formats_ = false;
  std::vector<fuchsia::hardware::audio::PcmSupportedFormats> pcm_formats_;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_TEST_BASE_H_
