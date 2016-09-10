// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A bootstrapping mojo application which also imlements some parts of a
// 'device runner'.
// TODO(alhaad): Figure out a way to move the 'guts' of the 'device runner' out
// of this bootstrapping application.

#include <mojo/system/main.h>

#include "apps/modular/mojom_hack/story_manager.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace {

mojo::Array<uint8_t> UserIdentityArray(const std::string& username) {
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(username.length());
  for (size_t i = 0; i < username.length(); i++) {
    array[i] = static_cast<uint8_t>(username[i]);
  }
  return array;
}

class DeviceRunnerBoot : public mojo::ApplicationImplBase {
  void OnInitialize() override {
    if (args().size() != 3) {
      FTL_DLOG(INFO) << "mojo:device-shell expects 2 additional arguments.\n"
                     << "Usage: mojo:device-shell [user] [recipe]";
      return;
    }
    FTL_DLOG(INFO) << "device-shell started with command line arguments.";

    FTL_DLOG(INFO) << "Starting story-manager for: " << args()[1];
    ConnectToService(shell(), "mojo:story-manager", GetProxy(&launcher_));
    mojo::StructPtr<ledger::Identity> identity = ledger::Identity::New();
    identity->user_id = UserIdentityArray(args()[1]);
    launcher_->Launch(std::move(identity), [](bool success) {
      FTL_DLOG(INFO) << "story-manager launched.";
    });

    FTL_DLOG(INFO) << "Starting story: " << args()[2];
    ConnectToService(shell(), "mojo:story-manager", GetProxy(&story_provider_));
    story_provider_->StartNewStory(
        args()[2],
        [](mojo::InterfaceHandle<story_manager::Story> story) {});
  }

  mojo::InterfacePtr<story_manager::Launcher> launcher_;
  mojo::InterfacePtr<story_manager::StoryProvider> story_provider_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerBoot);
};

}  // namespace

MojoResult MojoMain(MojoHandle application_request) {
  DeviceRunnerBoot device_runner_boot;
  return mojo::RunApplication(application_request, &device_runner_boot);
}
