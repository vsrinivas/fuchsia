// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/modular/device_runner/device_runner.mojom.h"
#include "apps/modular/story_manager/story_manager.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

mojo::Array<uint8_t> UserIdentityArray(const std::string& username) {
  mojo::Array<uint8_t> array = mojo::Array<uint8_t>::New(username.length());
  for (size_t i = 0; i < username.length(); i++) {
    array[i] = static_cast<uint8_t>(username[i]);
  }
  return array;
}

class DeviceRunnerImpl : public device_runner::DeviceRunner {
 public:
  DeviceRunnerImpl(mojo::Shell* shell,
                   mojo::InterfaceHandle<device_runner::DeviceRunner>* service)
      : shell_(shell), binding_(this, service) {}
  ~DeviceRunnerImpl() override {}

 private:
  void Login(const mojo::String& username) override {
    FTL_DLOG(INFO) << "Received username: " << username;

    // TODO(alhaad): Once we have a better understanding of lifecycle
    // management, revisit this.
    mojo::ConnectToService(shell_, "mojo:story_manager",
                           mojo::GetProxy(&launcher_));
    mojo::StructPtr<ledger::Identity> identity = ledger::Identity::New();
    identity->user_id = UserIdentityArray(username);
    launcher_->Launch(std::move(identity), [](bool success) {
      FTL_DLOG(INFO) << "story-manager launched.";
    });
  }

  mojo::Shell* shell_;
  mojo::StrongBinding<device_runner::DeviceRunner> binding_;

  // Interface pointer to the |StoryManager| handle exposed by the Story
  // Manager. Currently, we maintain a single instance which means that
  // subsequent logins override previous ones.
  mojo::InterfacePtr<story_manager::StoryManager> launcher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerImpl);
};

class DeviceRunnerApp : public mojo::ApplicationImplBase {
 public:
  DeviceRunnerApp() {}
  ~DeviceRunnerApp() override {}

 private:
  void OnInitialize() override {
    FTL_DLOG(INFO) << "Starting device shell.";
    ConnectToService(shell(), "mojo:dummy_device_shell",
                     GetProxy(&device_shell_));
    mojo::InterfaceHandle<device_runner::DeviceRunner> service;
    new DeviceRunnerImpl(shell(), &service);
    device_shell_->SetDeviceRunner(std::move(service));
  }

  mojo::InterfacePtr<device_runner::DeviceShell> device_shell_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle application_request) {
  DeviceRunnerApp app;
  return mojo::RunApplication(application_request, &app);
}
