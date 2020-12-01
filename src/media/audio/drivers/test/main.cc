// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include "gtest/gtest.h"
#include "src/lib/fsl/io/device_watcher.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/drivers/test/test_base.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

static const struct {
  const char* path;
  DeviceType device_type;
} kAudioDevNodes[] = {
    {.path = "/dev/class/audio-input-2", .device_type = DeviceType::Input},
    {.path = "/dev/class/audio-output-2", .device_type = DeviceType::Output},
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
void DetectDevices() {
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
}

// TODO(fxbug.dev/65580): Convert to value-parameterized testing and the INSTANTIATE_TEST_SUITE_P
// macro, moving DetectDevices to a function called at static initialization time. If we cannot
// access the cmdline flags at that time, we may need to always register admin tests, and then skip
// them at runtime based on the cmdline flag.

extern void RegisterBasicTestsForDevice(const DeviceEntry& device_entry);
extern void RegisterAdminTestsForDevice(const DeviceEntry& device_entry);

// Create testcase instances for each device entry.
void RegisterTests(bool test_admin_functions) {
  for (auto& device_entry : device_entries()) {
    RegisterBasicTestsForDevice(device_entry);

    if (test_admin_functions) {
      RegisterAdminTestsForDevice(device_entry);
    }
  }
}

};  // namespace media::audio::drivers::test

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetTestSettings(command_line)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  syslog::SetTags({"audio_driver_tests"});

  // --admin  Validate commands that require the privileged channel, such as SetFormat.
  bool test_admin_functions = command_line.HasOption("admin");

  media::audio::drivers::test::DetectDevices();
  media::audio::drivers::test::RegisterTests(test_admin_functions);

  return RUN_ALL_TESTS();
}
