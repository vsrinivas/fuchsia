// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/hermetic_audio_environment.h"

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/ultrasound/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio::test {
namespace {

// The IsolatedDevmgr will expose a fuchsia.io.Directory protocol under this service name in the
// devmgrs public directory.
constexpr const char kIsolatedDevmgrServiceName[] = "fuchsia.media.AudioTestDevmgr";

fit::function<fuchsia::sys::LaunchInfo()> LaunchInfoWithIsolatedDevmgrForUrl(
    const char* url, std::vector<std::string> args,
    std::shared_ptr<sys::ServiceDirectory> services) {
  return [url, args = std::move(args), services = std::move(services)] {
    zx::channel devfs = services->Connect<fuchsia::io::Directory>(kIsolatedDevmgrServiceName)
                            .Unbind()
                            .TakeChannel();
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.arguments = args;
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/dev");
    launch_info.flat_namespace->directories.push_back(std::move(devfs));
    return launch_info;
  };
}

fit::function<fuchsia::sys::LaunchInfo()> AudioLaunchInfo(
    std::shared_ptr<sys::ServiceDirectory> services) {
  return LaunchInfoWithIsolatedDevmgrForUrl("fuchsia-pkg://fuchsia.com/audio#meta/audio.cmx", {},
                                            services);
}

fit::function<fuchsia::sys::LaunchInfo()> AudioCoreLaunchInfo(
    std::shared_ptr<sys::ServiceDirectory> services) {
  return LaunchInfoWithIsolatedDevmgrForUrl(
      "fuchsia-pkg://fuchsia.com/audio_core#meta/audio_core_nodevfs.cmx", {}, services);
}

fit::function<fuchsia::sys::LaunchInfo()> VirtualAudioLaunchInfo(
    std::shared_ptr<sys::ServiceDirectory> services) {
  return LaunchInfoWithIsolatedDevmgrForUrl(
      "fuchsia-pkg://fuchsia.com/virtual_audio_service#meta/virtual_audio_service_nodevfs.cmx", {},
      services);
}

// Runs a thread with a dedicated loop for managing the enclosing environment. We use a thread here
// for a few reasons. First, and most importantly, we want to share a hermetic audio_core instance
// across all the tests in a test suite. To do this, we need to provide the EnclosingEnvironment
// with an async loop that is not scoped to the lifetime of a single test (as is done when using
// gtest::RealLoopFixture).
//
// Secondly, if we reuse the primary test loop we can under some circumstances run into deadlock
// when, for example, using a sync pointer since that will block the async loop before the backing
// service has a chance to be created.
void EnvironmentMain(HermeticAudioEnvironment* env) {
  async::Loop loop{&kAsyncLoopConfigAttachToCurrentThread};
  env->Start(&loop);
  loop.Run();
}

}  // namespace

HermeticAudioEnvironment::HermeticAudioEnvironment() {
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
  ConnectToService(devfs_dir.NewRequest(), kIsolatedDevmgrServiceName);
  fuchsia::io::NodeInfo info;
  zx_status_t status = devfs_dir->Describe(&info);
  FX_CHECK(status == ZX_OK);
}

void HermeticAudioEnvironment::Start(async::Loop* loop) {
  FX_CHECK(loop_ == nullptr);
  loop_ = loop;
  auto real_services = sys::ServiceDirectory::CreateFromNamespace();
  auto real_env = real_services->Connect<fuchsia::sys::Environment>();
  // Add in the services that will be available in our hermetic environment. Note the '_nodevfs'
  // cmx files; these are needed to allow us to map in our isolated devmgr under /dev for each
  // component, otherwise these components would still be provided the shared/global devmgr.
  auto services = sys::testing::EnvironmentServices::Create(real_env);
  services->AddServiceWithLaunchInfo("audio_core", AudioCoreLaunchInfo(real_services),
                                     fuchsia::media::ActivityReporter::Name_);
  services->AddServiceWithLaunchInfo("audio_core", AudioCoreLaunchInfo(real_services),
                                     fuchsia::media::AudioCore::Name_);
  services->AddServiceWithLaunchInfo("audio_core", AudioCoreLaunchInfo(real_services),
                                     fuchsia::media::AudioDeviceEnumerator::Name_);
  services->AddServiceWithLaunchInfo("audio", AudioLaunchInfo(real_services),
                                     fuchsia::media::Audio::Name_);
  services->AddServiceWithLaunchInfo("audio_core", AudioCoreLaunchInfo(real_services),
                                     fuchsia::media::UsageReporter::Name_);
  services->AddServiceWithLaunchInfo("audio_core", AudioCoreLaunchInfo(real_services),
                                     fuchsia::ultrasound::Factory::Name_);
  services->AddServiceWithLaunchInfo("virtual_audio", VirtualAudioLaunchInfo(real_services),
                                     fuchsia::virtualaudio::Control::Name_);
  services->AddServiceWithLaunchInfo("virtual_audio", VirtualAudioLaunchInfo(real_services),
                                     fuchsia::virtualaudio::Input::Name_);
  services->AddServiceWithLaunchInfo("virtual_audio", VirtualAudioLaunchInfo(real_services),
                                     fuchsia::virtualaudio::Output::Name_);
  services->AllowParentService("fuchsia.logger.LogSink");
  services->AllowParentService(fuchsia::scheduler::ProfileProvider::Name_);
  services->AllowParentService(kIsolatedDevmgrServiceName);

  std::unique_lock<std::mutex> lock(mutex_);
  hermetic_environment_ =
      sys::testing::EnclosingEnvironment::Create("audio_test", real_env, std::move(services), {});
  hermetic_environment_->SetRunningChangedCallback([this](bool running) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (running) {
      cv_.notify_all();
    }
  });
}

HermeticAudioEnvironment::~HermeticAudioEnvironment() {
  if (loop_) {
    async::PostTask(loop_->dispatcher(), [loop = loop_] { loop->Quit(); });
    env_thread_.join();
  }
}

}  // namespace media::audio::test
