// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/plug_detector.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace media::audio {
namespace {

// A minimal |fuchsia::hardware::audio::Device| that we can use to emulate a fake devfs directory
// for testing.
class FakeAudioDevice : public fuchsia::hardware::audio::StreamConfigConnector,
                        public fuchsia::hardware::audio::StreamConfig {
 public:
  FakeAudioDevice() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("fake-audio-device-loop");
  }
  ~FakeAudioDevice() { loop_.Shutdown(); }

  fbl::RefPtr<fs::Service> AsService() {
    return fbl::MakeRefCounted<fs::Service>([this](zx::channel c) {
      connector_binding_.Bind(std::move(c));
      return ZX_OK;
    });
  }

  bool is_bound() const { return stream_config_binding_->is_bound(); }

 private:
  // FIDL method for fuchsia.hardware.audio.StreamConfigConnector.
  void Connect(fidl::InterfaceRequest<fuchsia::hardware::audio::StreamConfig> server) override {
    stream_config_binding_.emplace(this, std::move(server), loop_.dispatcher());
  }

  // FIDL methods for fuchsia.hardware.audio.StreamConfig.
  void GetProperties(GetPropertiesCallback callback) override { callback({}); }
  void GetSupportedFormats(GetSupportedFormatsCallback callback) override { callback({}); }
  void CreateRingBuffer(
      ::fuchsia::hardware::audio::Format format,
      ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> intf) override {}
  void WatchGainState(WatchGainStateCallback callback) override { callback({}); }
  void SetGain(::fuchsia::hardware::audio::GainState target_state) override {}
  void WatchPlugState(WatchPlugStateCallback callback) override { callback({}); }
  void GetHealthState(GetHealthStateCallback callback) override { callback({}); }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing) override {
    signal_processing.Close(ZX_ERR_NOT_SUPPORTED);
  }

  async::Loop loop_;
  std::optional<fidl::Binding<fuchsia::hardware::audio::StreamConfig>> stream_config_binding_;
  fidl::Binding<fuchsia::hardware::audio::StreamConfigConnector> connector_binding_{this};
};

class DeviceTracker {
 public:
  struct DeviceConnection {
    std::string name;
    bool is_input;
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config;
  };

  fit::function<void(std::string, bool,
                     fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig>)>
  GetHandler() {
    return [this](auto name, auto is_input, auto stream_config) {
      // To make sure the 1-way Connect call is completed in the StreamConfigConnector server,
      // make a 2-way call. Since StreamConfigConnector does not have a 2-way call, we use
      // StreamConfig synchronously.
      fidl::SynchronousInterfacePtr client = stream_config.BindSync();
      fuchsia::hardware::audio::StreamProperties unused_properties;
      client->GetProperties(&unused_properties);
      stream_config = client.Unbind();
      devices_.emplace_back(DeviceConnection{std::move(name), is_input, std::move(stream_config)});
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

    // Serve up the emulated audio-input directory
    ASSERT_EQ(zx::channel::create(0, &c1, &c2), ZX_OK);
    ASSERT_EQ(vfs_.Serve(input_dir_, std::move(c1), fs::VnodeConnectionOptions::ReadOnly()), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, "/dev/class/audio-input", c2.release()), ZX_OK);

    // Serve up the emulated audio-output directory
    ASSERT_EQ(zx::channel::create(0, &c1, &c2), ZX_OK);
    ASSERT_EQ(vfs_.Serve(output_dir_, std::move(c1), fs::VnodeConnectionOptions::ReadOnly()),
              ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns_, "/dev/class/audio-output", c2.release()), ZX_OK);
  }
  void TearDown() override {
    ASSERT_TRUE(input_dir_->IsEmpty());
    ASSERT_TRUE(output_dir_->IsEmpty());
    vfs_loop_.Shutdown();
    vfs_loop_.JoinThreads();
    ASSERT_NE(ns_, nullptr);
    ASSERT_EQ(fdio_ns_unbind(ns_, "/dev/class/audio-input"), ZX_OK);
    ASSERT_EQ(fdio_ns_unbind(ns_, "/dev/class/audio-output"), ZX_OK);
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

  // Adds a |FakeAudioDevice| to the emulated 'audio-input' directory that has been installed in
  // the local namespace at /dev/class/audio-input.
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

TEST_F(PlugDetectorTest, DetectExistingDevices) {
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

TEST_F(PlugDetectorTest, DetectHotplugDevices) {
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

}  // namespace
}  // namespace media::audio
