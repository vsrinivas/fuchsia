// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <set>

#include "gtest/gtest.h"
#include "src/lib/fsl/io/device_watcher.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/drivers/test/test_base.h"

namespace media::audio::drivers::test {

static const struct {
  const char* path;
  DeviceType device_type;
} kAudioDevNodes[] = {
    {.path = "/dev/class/audio-input", .device_type = DeviceType::Input},
    {.path = "/dev/class/audio-output", .device_type = DeviceType::Output},
};

// static
static std::vector<std::unique_ptr<fsl::DeviceWatcher>>& device_watchers() {
  static std::vector<std::unique_ptr<fsl::DeviceWatcher>>* device_watchers =
      new std::vector<std::unique_ptr<fsl::DeviceWatcher>>();
  return *device_watchers;
}

static std::set<DeviceEntry>& device_entries() {
  static std::set<DeviceEntry>* device_entries = new std::set<DeviceEntry>();
  return *device_entries;
}

// Called once, before RUN_ALL_TESTS() is invoked. This generates the set of device entries.
void AddDevices(bool devfs_only = false) {
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};

  // Set up the watchers, etc. If any fail, automatically stop monitoring all device sources.
  for (const auto& devnode : kAudioDevNodes) {
    bool initial_enumeration_done = false;

    auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
        devnode.path,
        [dev_type = devnode.device_type](int dir_fd, const std::string& filename) {
          FX_LOGS(TRACE) << "dir_fd " << dir_fd << " for '" << filename << "'";
          device_entries().insert({dir_fd, filename, dev_type});
        },
        [&initial_enumeration_done]() { initial_enumeration_done = true; });

    if (watcher == nullptr) {
      ASSERT_FALSE(watcher == nullptr)
          << "AudioDriver::TestBase failed creating DeviceWatcher for '" << devnode.path << "'.";
    }
    device_watchers().emplace_back(std::move(watcher));

    while (!initial_enumeration_done) {
      loop.Run(zx::deadline_after(zx::msec(1)));
    }
  }

  if (!devfs_only) {
    // Unless expressly excluded, add a device entry for the a2dp-source output device driver.
    // This validates admin functionality even if AudioCore has connected to "real" audio drivers.
    device_entries().insert({DeviceEntry::kA2dp, "audio-a2dp", DeviceType::Output});
  }
}

// TODO(fxbug.dev/65580): Previous implementation used value-parameterized testing. Consider
// reverting to this, moving AddDevices to a function called at static initialization time. If we
// cannot access cmdline flags at that time, this would force us to always register admin tests,
// skipping them at runtime based on the cmdline flag.

extern void RegisterBasicTestsForDevice(const DeviceEntry& device_entry);
extern void RegisterAdminTestsForDevice(const DeviceEntry& device_entry,
                                        bool expect_audio_core_connected);
extern void RegisterPositionTestsForDevice(const DeviceEntry& device_entry,
                                           bool expect_audio_core_connected,
                                           bool enable_position_tests);

// Create testcase instances for each device entry.
void RegisterTests(bool expect_audio_core_connected, bool enable_position_tests) {
  for (auto& device_entry : device_entries()) {
    RegisterBasicTestsForDevice(device_entry);
    RegisterAdminTestsForDevice(device_entry, expect_audio_core_connected);
    RegisterPositionTestsForDevice(device_entry, expect_audio_core_connected,
                                   enable_position_tests);
  }
}

}  // namespace media::audio::drivers::test

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetTestSettings(command_line)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  syslog::SetTags({"audio_driver_tests"});

  // --admin: Validate commands that require the privileged channel, such as SetFormat.
  //   Otherwise, omit AdminTest cases if a device/driver is exposed in the device tree.
  //   TODO(fxbug.dev/93428): Enable tests if we see audio_core isn't connected to drivers.
  bool expect_audio_core_connected = !command_line.HasOption("admin");

  // --devfs-only: Only test devices detected in devfs; don't add/test Bluetooth audio a2dp output.
  bool devfs_only = command_line.HasOption("devfs-only");

  // --run-position-tests: Include audio position test cases (requires realtime capable system).
  bool enable_position_tests = command_line.HasOption("run-position-tests");

  media::audio::drivers::test::AddDevices(devfs_only);
  media::audio::drivers::test::RegisterTests(expect_audio_core_connected, enable_position_tests);

  return RUN_ALL_TESTS();
}
