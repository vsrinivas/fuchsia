// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/device_registry/device_detector.h"

#include <fidl/fuchsia.hardware.audio/cpp/fidl.h>
#include <fidl/fuchsia.hardware.audio/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/channel.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace media_audio {

namespace {

// Minimal `fuchsia_hardware_audio::Device` used to emulate a fake devfs directory for tests.
class FakeAudioDevice
    : public fidl::testing::WireTestBase<::fuchsia_hardware_audio::StreamConfigConnector>,
      public fidl::testing::WireTestBase<::fuchsia_hardware_audio::StreamConfig> {
 public:
  FakeAudioDevice(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  ~FakeAudioDevice() {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    ADD_FAILURE() << "Method not implemented: '" << name << "";
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Used synchronously, this 2-way call ensures that Connect is complete, before we proceed.
  void GetProperties(GetPropertiesCompleter::Sync& completer) override { completer.Reply({}); }

  fbl::RefPtr<fs::Service> AsService() {
    return fbl::MakeRefCounted<fs::Service>([this](zx::channel c) {
      connector_binding_ =
          fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::StreamConfigConnector>>(
              dispatcher(),
              fidl::ServerEnd<fuchsia_hardware_audio::StreamConfigConnector>(std::move(c)), this);
      return ZX_OK;
    });
  }

  bool is_bound() const { return stream_config_binding_.has_value(); }
  async_dispatcher_t* dispatcher() { return dispatcher_; }

 private:
  // FIDL method for fuchsia.hardware.audio.StreamConfigConnector.
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override {
    stream_config_binding_ =
        fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::StreamConfig>>(
            dispatcher(),
            fidl::ServerEnd<fuchsia_hardware_audio::StreamConfig>(std::move(request->protocol)),
            this);
  }

  async_dispatcher_t* dispatcher_;

  std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::StreamConfigConnector>>
      connector_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::StreamConfig>>
      stream_config_binding_;
};

class DeviceTracker {
 public:
  struct DeviceConnection {
    std::string_view name;
    fuchsia_audio_device::DeviceType device_type;
    fidl::Client<fuchsia_hardware_audio::StreamConfig> client;
  };

  DeviceTracker(async_dispatcher_t* dispatcher, bool detection_is_expected)
      : dispatcher_(dispatcher), detection_is_expected_(detection_is_expected) {}

  virtual ~DeviceTracker() = default;

  size_t size() const { return devices_.size(); }
  const std::vector<DeviceConnection>& devices() const { return devices_; }
  DeviceDetectionHandler handler() { return handler_; }
  async_dispatcher_t* dispatcher() { return dispatcher_; }

 private:
  DeviceDetectionHandler handler_ =
      [this](std::string_view name, fuchsia_audio_device::DeviceType device_type,
             fidl::ClientEnd<fuchsia_hardware_audio::StreamConfig> stream_config) {
        ASSERT_TRUE(detection_is_expected_) << "Unexpected device detection";
        auto client = fidl::Client<fuchsia_hardware_audio::StreamConfig>(
            fidl::ClientEnd<fuchsia_hardware_audio::StreamConfig>(std::move(stream_config)),
            dispatcher());

        devices_.emplace_back(DeviceConnection{std::move(name), device_type, std::move(client)});
      };
  async_dispatcher_t* dispatcher_;
  const bool detection_is_expected_;

  std::vector<DeviceConnection> devices_;
};

class DeviceDetectorTest : public gtest::TestLoopFixture {
 protected:
  static inline constexpr zx::duration kCommandTimeout = zx::sec(30);

  void SetUp() override {
    ASSERT_TRUE(input_dir_ != nullptr);
    ASSERT_TRUE(output_dir_ != nullptr);

    vfs_loop_.StartThread("vfs-loop");

    ASSERT_EQ(fdio_ns_get_installed(&ns_), ZX_OK);
    zx::channel channel0, channel1;

    // Serve up the emulated audio-input directory
    ASSERT_EQ(zx::channel::create(0, &channel0, &channel1), ZX_OK);
    ASSERT_EQ(vfs_.Serve(input_dir_, std::move(channel0), fs::VnodeConnectionOptions::ReadOnly()),
              ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, "/dev/class/audio-input", channel1.release()), ZX_OK);

    // Serve up the emulated audio-output directory
    ASSERT_EQ(zx::channel::create(0, &channel0, &channel1), ZX_OK);
    ASSERT_EQ(vfs_.Serve(output_dir_, std::move(channel0), fs::VnodeConnectionOptions::ReadOnly()),
              ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, "/dev/class/audio-output", channel1.release()), ZX_OK);
  }

  void TearDown() override {
    // Scoped directory entries have gone out of scope, but to avoid races we remove all entries.
    bool task_has_run = false;
    async::PostTask(vfs_loop_.dispatcher(), [this, &task_has_run]() {
      input_dir_->RemoveAllEntries();
      output_dir_->RemoveAllEntries();
      task_has_run = true;
    });
    while (!task_has_run) {
      RunVfsLoopUntilIdle();
    }
    ASSERT_TRUE(input_dir_->IsEmpty() && output_dir_->IsEmpty())
        << "input_dir is " << (input_dir_->IsEmpty() ? "" : "NOT ") << "empty; output_dir is "
        << (output_dir_->IsEmpty() ? "" : "NOT ") << "empty";

    vfs_loop_.Shutdown();
    vfs_loop_.JoinThreads();
    ASSERT_NE(ns_, nullptr);
    ASSERT_EQ(fdio_ns_unbind(ns_, "/dev/class/audio-input"), ZX_OK);
    ASSERT_EQ(fdio_ns_unbind(ns_, "/dev/class/audio-output"), ZX_OK);
  }

  void RunVfsLoopUntilIdle() { vfs_loop_.RunUntilIdle(); }

  // Holds a ref to a pseudo dir entry that removes the entry when this object goes out of scope.
  struct ScopedDirent {
    std::string name;
    fbl::RefPtr<fs::PseudoDir> dir;
    async_dispatcher_t* dispatcher;
    ~ScopedDirent() {
      async::PostTask(dispatcher, [n = name, d = dir]() { d->RemoveEntry(n); });
    }
  };

  // Adds a `FakeAudioDevice` to the emulated 'audio-input' directory that has been installed in
  // the local namespace at /dev/class/audio-input.
  ScopedDirent AddInputDevice(std::shared_ptr<FakeAudioDevice> device) {
    auto name = std::to_string(next_input_device_number_++);
    bool task_has_run = false;
    async::PostTask(vfs_loop_.dispatcher(), [this, name, device, &task_has_run]() {
      FX_CHECK(ZX_OK == input_dir_->AddEntry(name, device->AsService()));
      task_has_run = true;
    });
    while (!task_has_run) {
      RunVfsLoopUntilIdle();  // Switch to the VFS thread so it can add the pseudodir entry.
    }
    return {name, input_dir_, vfs_loop_.dispatcher()};
  }

  // Adds a `FakeAudioDevice` to the emulated 'audio-output' directory that has been installed in
  // the local namespace at /dev/class/audio-output.
  ScopedDirent AddOutputDevice(std::shared_ptr<FakeAudioDevice> device) {
    auto name = std::to_string(next_output_device_number_++);
    bool task_has_run = false;
    async::PostTask(vfs_loop_.dispatcher(), [this, name, device, &task_has_run]() {
      FX_CHECK(ZX_OK == output_dir_->AddEntry(name, device->AsService()));
      task_has_run = true;
    });
    while (!task_has_run) {
      RunVfsLoopUntilIdle();  // Switch to the VFS thread so it can add the pseudodir entry.
    }
    return {name, output_dir_, vfs_loop_.dispatcher()};
  }

 private:
  fdio_ns_t* ns_ = nullptr;
  uint32_t next_input_device_number_ = 0;
  uint32_t next_output_device_number_ = 0;

  // We must run the vfs on its own loop because the plug detector has a blocking openat()
  // call that doesn't yield back to the main loop, so that we can detect the device.
  //
  // TODO(fxbug.dev/35145): Migrate to an async open so that we can share the same dispatcher in
  // this test and also remove more blocking logic from the audio service.
  async::Loop vfs_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::SynchronousVfs vfs_{vfs_loop_.dispatcher()};

  // Note these _must_ be RefPtrs since vfs_ will try to AdoptRef on the raw pointer passed to it.
  //
  // TODO(fxbug.dev/35505): Migrate to //sdk/lib/vfs/cpp once that supports watching on PseudoDir.
  fbl::RefPtr<fs::PseudoDir> input_dir_{fbl::MakeRefCounted<fs::PseudoDir>()};
  fbl::RefPtr<fs::PseudoDir> output_dir_{fbl::MakeRefCounted<fs::PseudoDir>()};
};

// For devices that exist before the plug detector, verify pre-Start, post-Start, post-Stop.
TEST_F(DeviceDetectorTest, DetectExistingDevices) {
  // Add some devices that will exist before the plug detector is created.
  auto input0 = std::make_shared<FakeAudioDevice>(dispatcher());
  auto output0 = std::make_shared<FakeAudioDevice>(dispatcher());
  auto input1 = std::make_shared<FakeAudioDevice>(dispatcher());
  auto output1 = std::make_shared<FakeAudioDevice>(dispatcher());

  [[maybe_unused]] auto dev0 = AddInputDevice(input0);
  [[maybe_unused]] auto dev1 = AddOutputDevice(output0);
  [[maybe_unused]] auto dev2 = AddOutputDevice(output1);
  [[maybe_unused]] auto dev3 = AddInputDevice(input1);

  auto tracker = std::make_shared<DeviceTracker>(dispatcher(), true);
  RunLoopUntilIdle();
  ASSERT_EQ(0u, tracker->size());
  {
    // Create the detector; expect 4 events (1 for each device above);
    auto device_detector = DeviceDetector::Create(tracker->handler(), dispatcher());
    zx::time deadline = zx::clock::get_monotonic() + kCommandTimeout;
    while (zx::clock::get_monotonic() < deadline) {
      // A FakeAudioDevice could still be setting up its StreamConfig server end, by the time the
      // tracker adds it. We wait for the tracker AND the server-ends, to avoid a race.
      if (input0->is_bound() && output0->is_bound() && input1->is_bound() && output1->is_bound() &&
          tracker->size() >= 4) {
        break;
      }
      RunLoopUntilIdle();
    }
    RunLoopUntilIdle();  // Allow erroneous extra device additions to reveal themselves.
    ASSERT_EQ(tracker->size(), 4u) << "Timed out waiting for preexisting devices to be detected";

    int num_inputs = 0, num_outputs = 0;
    for (auto dev_num = 0u; dev_num < tracker->size(); ++dev_num) {
      auto& device = tracker->devices()[dev_num];
      EXPECT_TRUE(device.client.is_valid());
      if (device.device_type == fuchsia_audio_device::DeviceType::kInput) {
        ++num_inputs;
      } else {
        ++num_outputs;
      }
    }
    EXPECT_EQ(num_inputs, 2);
    EXPECT_EQ(num_outputs, 2);
  }

  RunLoopUntilIdle();  // Allow any erroneous device unbinds to reveal themselves.

  // After the detector is gone, preexisting devices we detected should still be bound.
  std::for_each(tracker->devices().begin(), tracker->devices().end(),
                [](const auto& device) { EXPECT_TRUE(device.client.is_valid()); });

  EXPECT_TRUE(input0->is_bound());
  EXPECT_TRUE(input1->is_bound());
  EXPECT_TRUE(output0->is_bound());
  EXPECT_TRUE(output1->is_bound());
}

// For devices added after the plug detector, verify detection (and post-detector persistence).
TEST_F(DeviceDetectorTest, DetectHotplugDevices) {
  auto input = std::make_shared<FakeAudioDevice>(dispatcher());
  auto output = std::make_shared<FakeAudioDevice>(dispatcher());

  auto tracker = std::make_shared<DeviceTracker>(dispatcher(), true);
  {
    auto device_detector = DeviceDetector::Create(tracker->handler(), dispatcher());

    RunLoopUntilIdle();
    ASSERT_EQ(0u, tracker->size());

    // Hotplug an input device and an output device.
    [[maybe_unused]] auto dev0 = AddInputDevice(input);
    zx::time deadline = zx::clock::get_monotonic() + kCommandTimeout;
    while (zx::clock::get_monotonic() < deadline) {
      // Wait for both tracker and device, same as above.
      if (tracker->size() >= 1u && input->is_bound()) {
        break;
      }
      RunLoopUntilIdle();
    }
    RunLoopUntilIdle();  // Allow erroneous extra device additions to reveal themselves.
    ASSERT_EQ(tracker->size(), 1u) << "Timed out waiting for input device to be detected";

    [[maybe_unused]] auto dev1 = AddOutputDevice(output);
    deadline = zx::clock::get_monotonic() + kCommandTimeout;
    while (zx::clock::get_monotonic() < deadline) {
      // Wait for both tracker and device, same as above.
      if (tracker->size() >= 2u && output->is_bound()) {
        break;
      }
      RunLoopUntilIdle();
    }
    RunLoopUntilIdle();  // Allow erroneous extra device additions to reveal themselves.
    ASSERT_EQ(tracker->size(), 2u) << "Incorrect number of devices was detected";

    std::for_each(tracker->devices().begin(), tracker->devices().end(),
                  [](const auto& device) { EXPECT_TRUE(device.client.is_valid()); });

    EXPECT_EQ(tracker->devices()[0].device_type, fuchsia_audio_device::DeviceType::kInput);
    EXPECT_EQ(tracker->devices()[1].device_type, fuchsia_audio_device::DeviceType::kOutput);
  }

  // After the device detector is gone, dynamically-detected devices should still be bound.
  RunLoopUntilIdle();  // Allow any erroneous device unbinds to reveal themselves.

  std::for_each(tracker->devices().begin(), tracker->devices().end(),
                [](const auto& device) { EXPECT_TRUE(device.client.is_valid()); });

  EXPECT_TRUE(input->is_bound());
  EXPECT_TRUE(output->is_bound());
}

// Ensure that once the plug detector is destroyed, detection handlers are no longer called.
TEST_F(DeviceDetectorTest, NoDanglingDetectors) {
  auto input = std::make_shared<FakeAudioDevice>(dispatcher());
  auto output = std::make_shared<FakeAudioDevice>(dispatcher());
  auto tracker = std::make_shared<DeviceTracker>(dispatcher(), false);

  {
    auto device_detector = DeviceDetector::Create(tracker->handler(), dispatcher());
    RunLoopUntilIdle();  // Allow erroneous device handler callbacks to reveal themselves.
    ASSERT_EQ(0u, tracker->size());
  }
  // After the device detector is gone, additional devices should not be detected.

  // Hotplug an input device and an output device.
  // If a device-detection handler is still in place, these will be inserted into tracker's list.
  [[maybe_unused]] auto dev0 = AddInputDevice(input);
  [[maybe_unused]] auto dev1 = AddOutputDevice(output);
  RunLoopUntilIdle();  // Allow erroneous device handler callbacks to reveal themselves.
  EXPECT_EQ(0u, tracker->size());
  EXPECT_FALSE(input->is_bound());
  EXPECT_FALSE(output->is_bound());
}

}  // namespace

}  // namespace media_audio
