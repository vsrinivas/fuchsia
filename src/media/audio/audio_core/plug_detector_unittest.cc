// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/plug_detector.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

namespace media::audio {
namespace {

// A minimal |fuchsia::hardware::audio::Device| that we can use to emulate a fake devfs directory
// for testing.
class FakeAudioDevice : public fuchsia::hardware::audio::Device {
 public:
  FakeAudioDevice() { FX_CHECK(zx::channel::create(0, &client_, &server_) == ZX_OK); }

  fbl::RefPtr<fs::Service> AsService() {
    return fbl::MakeRefCounted<fs::Service>([this](zx::channel c) {
      binding_.Bind(std::move(c));
      return ZX_OK;
    });
  }

  bool is_bound() const { return !client_; }

 private:
  void GetChannel(GetChannelCallback callback) override {
    FX_CHECK(client_);
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};
    stream_config.set_channel(std::move(client_));
    callback(std::move(stream_config));
  }

  zx::channel client_, server_;
  fidl::Binding<fuchsia::hardware::audio::Device> binding_{this};
};

class DeviceTracker {
 public:
  struct DeviceConnection {
    zx::channel channel;
    std::string name;
    bool is_input;
  };

  fit::function<void(zx::channel, std::string, bool, AudioDriverVersion)> GetHandler() {
    return [this](auto channel, auto name, auto is_input, auto version) {
      devices_.emplace_back(DeviceConnection{std::move(channel), std::move(name), is_input});
    };
  }

  size_t size() const { return devices_.size(); }

  std::vector<DeviceConnection> take_devices() { return std::move(devices_); }

 private:
  std::vector<DeviceConnection> devices_;
};

class PlugDetectorTest : public gtest::RealLoopFixture,
                         public ::testing::WithParamInterface<const char*> {
 protected:
  void SetUp() override {
    vfs_loop_.StartThread("vfs-loop");
    ASSERT_EQ(fdio_ns_get_installed(&ns_), ZX_OK);
    zx::channel c1, c2;

    // Serve up the emulated audio-input[-2] directory
    ASSERT_EQ(zx::channel::create(0, &c1, &c2), ZX_OK);
    ASSERT_EQ(vfs_.Serve(input_dir_, std::move(c1), fs::VnodeConnectionOptions::ReadOnly()), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, (std::string("/dev/class/audio-input") + GetParam()).c_str(),
                           c2.release()),
              ZX_OK);

    // Serve up the emulated audio-output[-2] directory
    ASSERT_EQ(zx::channel::create(0, &c1, &c2), ZX_OK);
    ASSERT_EQ(vfs_.Serve(output_dir_, std::move(c1), fs::VnodeConnectionOptions::ReadOnly()),
              ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, (std::string("/dev/class/audio-output") + GetParam()).c_str(),
                           c2.release()),
              ZX_OK);
  }
  void TearDown() override {
    ASSERT_TRUE(input_dir_->IsEmpty());
    ASSERT_TRUE(output_dir_->IsEmpty());
    vfs_loop_.Shutdown();
    vfs_loop_.JoinThreads();
    ASSERT_NE(ns_, nullptr);
    ASSERT_EQ(fdio_ns_unbind(ns_, (std::string("/dev/class/audio-input") + GetParam()).c_str()),
              ZX_OK);
    ASSERT_EQ(fdio_ns_unbind(ns_, (std::string("/dev/class/audio-output") + GetParam()).c_str()),
              ZX_OK);
  }

  // Holds a reference to a pseudo dir entry that removes the entry when this object goes out of
  // scope.
  struct ScopedDirent {
    std::string name;
    fbl::RefPtr<fs::PseudoDir> dir;
    ~ScopedDirent() {
      if (dir) {
        dir->RemoveEntry(name);
      }
    }
  };

  // Adds a |FakeAudioDevice| to the emulated 'audio-input[-2]' directory that has been installed in
  // the local namespace at /dev/class/audio-input[-2].
  ScopedDirent AddInputDevice(FakeAudioDevice* device) {
    auto name = std::to_string(next_input_device_number_++);
    FX_CHECK(ZX_OK == input_dir_->AddEntry(name, device->AsService()));
    return {name, input_dir_};
  }

  // Adds a |FakeAudioDevice| to the emulated 'audio-output' directory that has been installed in
  // the local namespace at /dev/class/audio-output.
  ScopedDirent AddOutputDevice(FakeAudioDevice* device) {
    auto name = std::to_string(next_output_device_number_++);
    FX_CHECK(ZX_OK == output_dir_->AddEntry(name, device->AsService()));
    return {name, output_dir_};
  }

 private:
  fdio_ns_t* ns_ = nullptr;
  uint32_t next_input_device_number_ = 0;
  uint32_t next_output_device_number_ = 0;

  // We need to run the vfs on its own loop because the plug detector has some blocking open()
  // calls that don't yield back to the main loop so that we can populate the device.
  //
  // TODO(fxbug.dev/35145): Migrate to an async open so that we can share the same dispatcher in
  // this test and also remove more blocking logic from audio_core.
  async::Loop vfs_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::SynchronousVfs vfs_{vfs_loop_.dispatcher()};
  // Note these _must_ be RefPtrs since the vfs_ will attempt to AdoptRef on a raw pointer passed
  // to it.
  //
  // TODO(fxbug.dev/35505): Migrate to //sdk/lib/vfs/cpp once that supports watching on PseudoDir.
  fbl::RefPtr<fs::PseudoDir> input_dir_{fbl::MakeRefCounted<fs::PseudoDir>()};
  fbl::RefPtr<fs::PseudoDir> output_dir_{fbl::MakeRefCounted<fs::PseudoDir>()};
};

TEST_P(PlugDetectorTest, DetectExistingDevices) {
  // Add some devices that will exist before the plug detector starts.
  FakeAudioDevice input0, input1;
  auto d1 = AddInputDevice(&input0);
  auto d2 = AddInputDevice(&input1);
  FakeAudioDevice output0, output1;
  auto d3 = AddOutputDevice(&output0);
  auto d4 = AddOutputDevice(&output1);

  // Create the plug detector; no events should be sent until |Start|.
  DeviceTracker tracker;
  auto plug_detector = PlugDetector::Create();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, tracker.size());

  // Start the detector; expect 4 events (1 for each device above);
  ASSERT_EQ(ZX_OK, plug_detector->Start(tracker.GetHandler()));
  RunLoopUntil([&tracker] { return tracker.size() == 4; });
  EXPECT_EQ(4u, tracker.size());
  EXPECT_TRUE(input0.is_bound());
  EXPECT_TRUE(input1.is_bound());
  EXPECT_TRUE(output0.is_bound());
  EXPECT_TRUE(output1.is_bound());

  plug_detector->Stop();
}

TEST_P(PlugDetectorTest, DetectHotplugDevices) {
  DeviceTracker tracker;
  auto plug_detector = PlugDetector::Create();
  ASSERT_EQ(ZX_OK, plug_detector->Start(tracker.GetHandler()));
  RunLoopUntilIdle();
  EXPECT_EQ(0u, tracker.size());

  // Hotplug a device.
  FakeAudioDevice input0;
  auto d1 = AddInputDevice(&input0);
  RunLoopUntil([&tracker] { return tracker.size() == 1; });
  ASSERT_EQ(1u, tracker.size());
  auto device = std::move(*tracker.take_devices().begin());
  EXPECT_TRUE(device.is_input);
  EXPECT_TRUE(input0.is_bound());

  plug_detector->Stop();
}

// This allows us to pick /dev/class/audio-input and /dev/class/audio-input-2 (similar for output).
INSTANTIATE_TEST_SUITE_P(PlugDetectorTestInstance, PlugDetectorTest, ::testing::Values("", "-2"));

}  // namespace
}  // namespace media::audio
