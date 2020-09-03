// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_audio_environment.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/substitute.h"

namespace media::audio::test {
namespace {

// The label used for our hermetic audio environment.
constexpr const char kIsolatedEnvironmentLabel[] = "hermetic_audio_test";

// The IsolatedDevmgr will expose a fuchsia.io.Directory protocol under this service name in the
// devmgrs public directory.
constexpr const char kIsolatedDevmgrServiceName[] = "fuchsia.media.AudioTestDevmgr";

std::function<fuchsia::sys::LaunchInfo()> LaunchInfoWithIsolatedDevmgrForUrl(
    std::string url, std::shared_ptr<sys::ServiceDirectory> services,
    std::string config_data_path = "",
    std::vector<std::string> arguments = std::vector<std::string>()) {
  return [url, services = std::move(services), config_data_path, arguments] {
    zx::channel devfs = services->Connect<fuchsia::io::Directory>(kIsolatedDevmgrServiceName)
                            .Unbind()
                            .TakeChannel();
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/dev");
    launch_info.flat_namespace->directories.push_back(std::move(devfs));

    if (!arguments.empty()) {
      launch_info.arguments = arguments;
    }

    zx::channel config_data;
    if (config_data_path != "") {
      FX_LOGS(INFO) << "Using path '" << config_data_path << "' for /config/data directory for "
                    << url << ".";
      zx::channel remote;
      zx::channel::create(0, &config_data, &remote);
      zx_status_t status = fdio_open(
          config_data_path.c_str(),
          fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_FLAG_DIRECTORY, remote.release());
      if (status == ZX_OK) {
        launch_info.flat_namespace->paths.push_back("/config/data");
        launch_info.flat_namespace->directories.push_back(std::move(config_data));
      } else {
        FX_PLOGS(ERROR, status) << "Unable to open '" << config_data_path << ".";
      }
    } else {
      FX_LOGS(INFO) << "No config_data provided for " << url;
    }

    return launch_info;
  };
}

std::string ComponentManifestFromURL(std::string component_url) {
  const char* kMeta = "#meta/";
  auto k = component_url.find(kMeta);
  FX_CHECK(k != std::string::npos);
  return component_url.substr(k + strlen(kMeta));
}

}  // namespace

// Runs a thread with a dedicated loop for managing the enclosing environment. We use a thread here
// for a few reasons. First, and most importantly, we want to share a hermetic audio_core instance
// across all the tests in a test suite. To do this, we need to provide the EnclosingEnvironment
// with an async loop that is not scoped to the lifetime of a single test (as is done when using
// gtest::RealLoopFixture).
//
// Secondly, if we reuse the primary test loop we can under some circumstances run into deadlock
// when, for example, using a sync pointer since that will block the async loop before the backing
// service has a chance to be created.
void HermeticAudioEnvironment::EnvironmentMain(HermeticAudioEnvironment* env) {
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};
  env->StartEnvThread(&loop);
  loop.Run();
}

HermeticAudioEnvironment::HermeticAudioEnvironment(Options options) : options_(options) {
  // Create the thread here to ensure the rest of the class has fully initialized before starting
  // the new thread, which takes a reference to |this|.
  env_thread_ = std::thread(EnvironmentMain, this);

  // Wait for the worker thread to start and finish some initialization.
  std::unique_lock<std::mutex> lock(mutex_);
  while (!hermetic_environment_ || !hermetic_environment_->is_running()) {
    cv_.wait(lock);
  }

  // IsolatedDevmgr will not serve any messages on the directory until /dev/test/virtual_audio
  // is ready. Run a simple Describe operation to ensure the devmgr is ready for traffic.
  //
  // Note we specifically use the |TextFixture| overrides of the virtual methods. This is needed
  // because some test fixtures override these methods and include some asserts that will not
  // be valid when this is run.
  fuchsia::io::DirectorySyncPtr devfs_dir;
  devmgr_services_->Connect(devfs_dir.NewRequest(), kIsolatedDevmgrServiceName);
  fuchsia::io::NodeInfo info;
  zx_status_t status = devfs_dir->Describe(&info);
  FX_CHECK(status == ZX_OK);
}

void HermeticAudioEnvironment::StartEnvThread(async::Loop* loop) {
  FX_CHECK(loop_ == nullptr);
  loop_ = loop;
  auto real_services = sys::ServiceDirectory::CreateFromNamespace();
  auto real_env = real_services->Connect<fuchsia::sys::Environment>();

  fuchsia::sys::LaunchInfo devmgr_launch_info;
  // This URL should be made more flexible for future tests.
  devmgr_launch_info.url =
      "fuchsia-pkg://fuchsia.com/audio-core-api-tests#meta/audio-test-devmgr.cmx";
  devmgr_services_ =
      sys::ServiceDirectory::CreateWithRequest(&devmgr_launch_info.directory_request);

  // Launch AudioTestDevmgr per-environment.
  fuchsia::sys::LauncherPtr launcher;
  real_env->GetLauncher(launcher.NewRequest());
  launcher->CreateComponent(std::move(devmgr_launch_info), controller_.NewRequest());

  // The '_nodevfs' cmx files are needed to allow us to map in our isolated devmgr under /dev for
  // each component, otherwise these components would still be provided the shared/global devmgr.
  std::string audio_url = "fuchsia-pkg://fuchsia.com/audio#meta/audio.cmx";
  std::string audio_core_url = options_.audio_core_base_url;
  if (options_.audio_core_config_data_path != "") {
    // When a custom config is specified, don't bother loading the default config data.
    audio_core_url += "#meta/audio_core_nodevfs_noconfigdata.cmx";
  } else {
    audio_core_url += "#meta/audio_core_nodevfs.cmx";
  }
  // clang-format off
  std::string virtual_audio_url =
      "fuchsia-pkg://fuchsia.com/virtual-audio-service-for-test#meta/virtual_audio_service_nodevfs.cmx";
  // clang-format on

  // Add in the services that will be available in our hermetic environment.
  struct ComponentLaunchInfo {
    ComponentType type;
    std::string url;
    std::function<fuchsia::sys::LaunchInfo()> launch_info;
    std::vector<const char*> service_names;
  };
  std::vector<ComponentLaunchInfo> to_launch{
      {
          .type = kAudioComponent,
          .url = audio_url,
          .launch_info = LaunchInfoWithIsolatedDevmgrForUrl(audio_url, devmgr_services_),
          .service_names = {fuchsia::media::Audio::Name_},
      },
      {
          .type = kAudioCoreComponent,
          .url = audio_core_url,
          .launch_info = LaunchInfoWithIsolatedDevmgrForUrl(audio_core_url, devmgr_services_,
                                                            options_.audio_core_config_data_path,
                                                            options_.audio_core_arguments),
          .service_names =
              {
                  fuchsia::media::ActivityReporter::Name_,
                  fuchsia::media::AudioCore::Name_,
                  fuchsia::media::AudioDeviceEnumerator::Name_,
                  fuchsia::media::tuning::AudioTuner::Name_,
                  fuchsia::media::UsageGainReporter::Name_,
                  fuchsia::media::UsageReporter::Name_,
                  fuchsia::media::audio::EffectsController::Name_,
                  fuchsia::ultrasound::Factory::Name_,
              },
      },
      {
          .type = kVirtualAudioComponent,
          .url = virtual_audio_url,
          .launch_info = LaunchInfoWithIsolatedDevmgrForUrl(virtual_audio_url, devmgr_services_),
          .service_names =
              {
                  fuchsia::virtualaudio::Control::Name_,
                  fuchsia::virtualaudio::Input::Name_,
                  fuchsia::virtualaudio::Output::Name_,
              },
      },
  };

  auto services = sys::testing::EnvironmentServices::Create(real_env);
  for (auto& c : to_launch) {
    component_urls_[c.type] = c.url;
    for (auto n : c.service_names) {
      services->AddServiceWithLaunchInfo(c.url, c.launch_info, n);
    }
  }
  services->AllowParentService("fuchsia.logger.LogSink");
  services->AllowParentService("fuchsia.tracing.provider.Registry");
  services->AllowParentService(fuchsia::scheduler::ProfileProvider::Name_);

  std::unique_lock<std::mutex> lock(mutex_);
  hermetic_environment_ = sys::testing::EnclosingEnvironment::Create(
      kIsolatedEnvironmentLabel, real_env, std::move(services), {});
  hermetic_environment_->SetRunningChangedCallback([this](bool running) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (running) {
      cv_.notify_all();
    }
  });
}

HermeticAudioEnvironment::~HermeticAudioEnvironment() {
  FX_CHECK(loop_);
  async::PostTask(loop_->dispatcher(), [loop = loop_] { loop->Quit(); });
  env_thread_.join();
}

const inspect::Hierarchy HermeticAudioEnvironment::ReadInspect(ComponentType component_type) {
  auto it = component_urls_.find(component_type);
  FX_CHECK(it != component_urls_.end()) << "unknown component " << component_type;

  files::Glob glob(fxl::Substitute("/hub/r/$0/*/c/$1/*/out/diagnostics/fuchsia.inspect.Tree",
                                   kIsolatedEnvironmentLabel,
                                   ComponentManifestFromURL(it->second)));
  FX_CHECK(glob.size() == 1) << "could not find unique fuchsia.inspect.Tree, found " << glob.size()
                             << " matches";

  auto path = std::string(*glob.begin());
  fuchsia::inspect::TreeSyncPtr tree;
  auto status = fdio_service_connect(path.c_str(), tree.NewRequest().TakeChannel().release());
  FX_CHECK(status == ZX_OK) << "could not connect to fuchsia.inspect.Tree: " << status;

  fuchsia::inspect::TreeContent c;
  status = tree->GetContent(&c);
  FX_CHECK(status == ZX_OK) << "could not get VMO from fuchsia.inspect.Tree" << status;
  FX_CHECK(c.has_buffer());

  return inspect::ReadFromVmo(c.buffer().vmo).take_value();
}

}  // namespace media::audio::test
