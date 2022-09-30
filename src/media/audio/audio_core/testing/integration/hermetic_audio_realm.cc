// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/integration/hermetic_audio_realm.h"

#include <fuchsia/driver/test/cpp/fidl.h>
#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/device-watcher/cpp/device-watcher.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

namespace media::audio::test {

namespace {
void ConnectToVirtualAudio(component_testing::RealmRoot& root,
                           fidl::SynchronousInterfacePtr<fuchsia::virtualaudio::Control>& out) {
  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = root.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd dev_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), dev_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  // This file hosts a fuchsia.virtualaudio.Control channel.
  std::string_view kControlName(fuchsia::virtualaudio::CONTROL_NODE_NAME);
  ASSERT_EQ(kControlName.substr(0, 5), "/dev/") << "unexpected file name: " << kControlName;
  std::string control_file_name(kControlName.substr(5));

  // Wait for the driver to load.
  fbl::unique_fd file_fd;
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile(dev_fd, control_file_name.c_str(), &file_fd));

  // Turn the connection into FIDL.
  zx_handle_t handle;
  ASSERT_EQ(ZX_OK, fdio_fd_clone(file_fd.get(), &handle));
  out.Bind(zx::channel(handle));
}

// Implements a simple component that serves fuchsia.audio.effects.ProcessorCreator
// using a TestEffectsV2.
class LocalProcessorCreator : public component_testing::LocalComponent {
 public:
  explicit LocalProcessorCreator(std::vector<TestEffectsV2::Effect> effects) : effects_(effects) {}

  void Start(std::unique_ptr<component_testing::LocalComponentHandles> mock_handles) override {
    handles_ = std::move(mock_handles);
    ASSERT_EQ(ZX_OK,
              handles_->outgoing()->AddPublicService(
                  std::make_unique<vfs::Service>([this](zx::channel channel,
                                                        async_dispatcher_t* dispatcher) {
                    if (!server_) {
                      server_ = std::make_unique<TestEffectsV2>(dispatcher);
                      for (auto& effect : effects_) {
                        server_->AddEffect(std::move(effect));
                      }
                    }
                    server_->HandleRequest(fidl::ServerEnd<fuchsia_audio_effects::ProcessorCreator>(
                        std::move(channel)));
                  }),
                  "fuchsia.audio.effects.ProcessorCreator"));
  }

 private:
  std::vector<TestEffectsV2::Effect> effects_;
  std::unique_ptr<TestEffectsV2> server_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
};

// Implements a simple component that exports the given local directory as a capability named "dir".
class LocalDirectoryExporter : public component_testing::LocalComponent {
 public:
  static inline const char kCapability[] = "exported-dir";
  static inline const char kPath[] = "/exported-dir";

  explicit LocalDirectoryExporter(const std::string& local_dir_name) {
    // Open a handle to the directory.
    zx::channel local, remote;
    auto status = zx::channel::create(0, &local, &remote);
    FX_CHECK(status == ZX_OK) << status;
    status = fdio_open(local_dir_name.c_str(),
                       static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                             fuchsia_io::wire::OpenFlags::kDirectory),
                       remote.release());
    FX_CHECK(status == ZX_OK) << status;
    local_dir_ = std::move(local);
  }

  void Start(std::unique_ptr<component_testing::LocalComponentHandles> mock_handles) override {
    handles_ = std::move(mock_handles);
    ASSERT_EQ(ZX_OK, handles_->outgoing()->root_dir()->AddEntry(
                         kCapability, std::make_unique<vfs::RemoteDir>(std::move(local_dir_))));
  }

 private:
  zx::channel local_dir_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
};
}  // namespace

// Cannot define these until LocalProcessorCreator is defined.
HermeticAudioRealm::HermeticAudioRealm(CtorArgs&& args)
    : root_(std::move(args.root)), local_components_(std::move(args.local_components)) {}
HermeticAudioRealm::~HermeticAudioRealm() = default;

// This returns `void` so we can ASSERT from within Create.
void HermeticAudioRealm::Create(Options options, async_dispatcher* dispatcher,
                                std::unique_ptr<HermeticAudioRealm>& realm_out) {
  // Build the realm.
  realm_out = std::unique_ptr<HermeticAudioRealm>(
      new HermeticAudioRealm(BuildRealm(std::move(options), dispatcher)));
  auto& realm = realm_out->root_;

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::RealmArgs realm_args;
  realm_args.set_root_driver("fuchsia-boot:///#driver/platform-bus.so");

  fuchsia::driver::test::Realm_Start_Result realm_result;
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(realm_args), &realm_result));
  ASSERT_FALSE(realm_result.is_err()) << "status = " << realm_result.err();

  // Hold a reference to fuchsia.virtualaudio.Control.
  ASSERT_NO_FATAL_FAILURE(ConnectToVirtualAudio(realm, realm_out->virtual_audio_control_));
}

HermeticAudioRealm::CtorArgs HermeticAudioRealm::BuildRealm(Options options,
                                                            async_dispatcher* dispatcher) {
  auto builder = component_testing::RealmBuilder::Create();

  using component_testing::ChildRef;
  using component_testing::Directory;
  using component_testing::DirectoryContents;
  using component_testing::LocalComponent;
  using component_testing::ParentRef;
  using component_testing::Protocol;
  std::vector<std::unique_ptr<LocalComponent>> local_components;

  builder.AddChild(kAudioCore, "#meta/audio_core.cm");

  // Route AudioCore -> test component.
  builder.AddRoute({
      .capabilities =
          {
              Protocol{"fuchsia.media.ActivityReporter"},
              Protocol{"fuchsia.media.Audio"},
              Protocol{"fuchsia.media.AudioCore"},
              Protocol{"fuchsia.media.AudioDeviceEnumerator"},
              Protocol{"fuchsia.media.audio.EffectsController"},
              Protocol{"fuchsia.media.tuning.AudioTuner"},
              Protocol{"fuchsia.media.UsageGainReporter"},
              Protocol{"fuchsia.media.UsageReporter"},
              Protocol{"fuchsia.ultrasound.Factory"},
          },
      .source = ChildRef{kAudioCore},
      .targets = {ParentRef()},
  });

  // Route test component -> AudioCore.
  builder.AddRoute({
      .capabilities =
          {
              Protocol{"fuchsia.logger.LogSink"},
              Protocol{"fuchsia.scheduler.ProfileProvider"},
              // Not necessary for tests but can be useful when debugging tests.
              Protocol{"fuchsia.tracing.provider.Registry"},
          },
      .source = ParentRef(),
      .targets = {ChildRef{kAudioCore}},
  });

  switch (options.audio_core_config_data.index()) {
    case 0:  // empty
      builder.RouteReadOnlyDirectory("config-data", {ChildRef{kAudioCore}}, DirectoryContents());
      break;
    case 1: {  // route from parent
      // Export the given local directory as AudioCore's config-data. To export a directory,
      // we need to publish it in a component's outgoing directory. The simplest way to do that
      // is to export the directory from a local component.
      auto dir = std::get<1>(options.audio_core_config_data).directory_name;
      auto exporter = std::make_unique<LocalDirectoryExporter>(dir);
      builder.AddLocalChild("local_config_data_exporter", exporter.get());
      builder.AddRoute({
          .capabilities = {Directory{
              .name = LocalDirectoryExporter::kCapability,
              .as = "config-data",
              .rights = fuchsia::io::R_STAR_DIR,
              .path = LocalDirectoryExporter::kPath,
          }},
          .source = ChildRef{"local_config_data_exporter"},
          .targets = {ChildRef{kAudioCore}},
      });
      local_components.push_back(std::move(exporter));
      break;
    }
    case 2:  // use specified files
      builder.RouteReadOnlyDirectory("config-data", {ChildRef{kAudioCore}},
                                     std::move(std::get<2>(options.audio_core_config_data)));
      break;
    default:
      FX_CHECK(false) << "unexpected index " << options.audio_core_config_data.index();
  }

  // If needed, add a local component to host effects-over-FIDL.
  if (!options.test_effects_v2.empty()) {
    auto local_processor_creator =
        std::make_unique<LocalProcessorCreator>(std::move(options.test_effects_v2));
    builder.AddLocalChild("local_processor_creator", local_processor_creator.get());
    builder.AddRoute({
        .capabilities = {Protocol{"fuchsia.audio.effects.ProcessorCreator"}},
        .source = ChildRef{"local_processor_creator"},
        .targets = {ChildRef{kAudioCore}},
    });
    local_components.push_back(std::move(local_processor_creator));
  }

  // Add a hermetic driver realm and route "/dev" to audio_core.
  driver_test_realm::Setup(builder);
  builder.AddRoute({
      .capabilities =
          {
              Directory{
                  .name = "dev",
                  .as = "dev-audio-input",
                  .subdir = "class/audio-input",
                  .path = "/dev/class/audio-input",
              },
              Directory{
                  .name = "dev",
                  .as = "dev-audio-output",
                  .subdir = "class/audio-output",
                  .path = "/dev/class/audio-output",
              },
          },
      .source = ChildRef{"driver_test_realm"},
      .targets = {ChildRef{kAudioCore}},
  });

  // Route some capabilities to the driver realm.
  builder.AddRoute({
      .capabilities = {Protocol{"fuchsia.logger.LogSink"}},
      .source = ParentRef(),
      .targets = {ChildRef{"driver_test_realm"}},
  });

  // Some tests need to control the thermal state.
  // For simplicity, always add this test thermal control server.
  builder.AddChild(kThermalTestControl, "#meta/thermal_test_control.cm");
  builder.AddRoute({
      .capabilities = {Protocol{"fuchsia.thermal.ClientStateConnector"}},
      .source = ChildRef{kThermalTestControl},
      .targets = {ChildRef{kAudioCore}},
  });
  builder.AddRoute({
      .capabilities = {Protocol{"test.thermal.ClientStateControl"}},
      .source = ChildRef{kThermalTestControl},
      .targets = {ParentRef()},
  });
  builder.AddRoute({
      .capabilities = {Protocol{"fuchsia.logger.LogSink"}},
      .source = ParentRef(),
      .targets = {ChildRef{kThermalTestControl}},
  });

  // Include a mock cobalt to silence warnings that we can't connect to cobalt.
  builder.AddChild(kMockCobalt, "#meta/mock_cobalt.cm");
  builder.AddRoute({
      .capabilities = {Protocol{"fuchsia.metrics.MetricEventLoggerFactory"}},
      .source = ChildRef{kMockCobalt},
      .targets = {ChildRef{kAudioCore}},
  });
  builder.AddRoute({
      .capabilities = {Protocol{"fuchsia.logger.LogSink"}},
      .source = ParentRef(),
      .targets = {ChildRef{kMockCobalt}},
  });

  // Make audio_core's fuchsia.inspect.Tree readable by the test.
  //
  // Each component that includes //sdk/lib/inspect/client.shard.cml exposes a "/diagnostics"
  // directory with a file named "/diagnostics/fuchsia.inspect.Tree", through which we can
  // connect to the protocol with that name. Hence we map "/diagnostics" into this process.
  builder.AddRoute({
      .capabilities = {Directory{
          .name = "diagnostics-for-integration-tests",
          .as = "diagnostics-audio-core",
      }},
      .source = ChildRef{kAudioCore},
      .targets = {ParentRef()},
  });

  // Lastly, allow further customization.
  if (options.customize_realm) {
    auto status = options.customize_realm(builder);
    FX_CHECK(status == ZX_OK) << "customize_realm failed with status=" << status;
  }

  return {builder.Build(dispatcher), std::move(local_components)};
}

const inspect::Hierarchy HermeticAudioRealm::ReadInspect(std::string_view component_name) {
  // Only supported component for now.
  FX_CHECK(component_name == kAudioCore);

  fuchsia::inspect::TreeSyncPtr tree;
  auto status = fdio_service_connect_at(root_.CloneRoot().TakeChannel().release(),
                                        "diagnostics-audio-core/fuchsia.inspect.Tree",
                                        tree.NewRequest().TakeChannel().release());
  FX_CHECK(status == ZX_OK) << "could not connect to fuchsia.inspect.Tree for component '"
                            << component_name << ": " << status;

  fuchsia::inspect::TreeContent c;
  status = tree->GetContent(&c);
  FX_CHECK(status == ZX_OK) << "could not get VMO from fuchsia.inspect.Tree: " << status;
  FX_CHECK(c.has_buffer());

  return inspect::ReadFromVmo(c.buffer().vmo).take_value();
}

}  // namespace media::audio::test
