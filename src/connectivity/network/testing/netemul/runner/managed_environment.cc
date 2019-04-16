// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_environment.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <src/lib/fxl/strings/string_printf.h>

#include <random>

namespace netemul {

// Start the Log and LogSink service (the same component publishses both
// services))
static const char* kLogSinkServiceURL =
    "fuchsia-pkg://fuchsia.com/logger#meta/logger.cmx";
static const char* kLogServiceURL =
    "fuchsia-pkg://fuchsia.com/logger#meta/logger.cmx";

using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;

ManagedEnvironment::Ptr ManagedEnvironment::CreateRoot(
    const fuchsia::sys::EnvironmentPtr& parent,
    const SandboxEnv::Ptr& sandbox_env, Options options) {
  auto ret = ManagedEnvironment::Ptr(new ManagedEnvironment(sandbox_env));
  ret->Create(parent, std::move(options));
  return ret;
}

ManagedEnvironment::ManagedEnvironment(const SandboxEnv::Ptr& sandbox_env)
    : sandbox_env_(sandbox_env) {}
sys::testing::EnclosingEnvironment& ManagedEnvironment::environment() {
  return *env_;
}

void ManagedEnvironment::GetLauncher(
    ::fidl::InterfaceRequest<::fuchsia::sys::Launcher> launcher) {
  launcher_->Bind(std::move(launcher));
}

void ManagedEnvironment::CreateChildEnvironment(
    fidl::InterfaceRequest<FManagedEnvironment> me, Options options) {
  ManagedEnvironment::Ptr np(new ManagedEnvironment(sandbox_env_));
  fuchsia::sys::EnvironmentPtr env;
  env_->ConnectToService(env.NewRequest());
  np->Create(env, std::move(options), this);
  np->bindings_.AddBinding(np.get(), std::move(me));

  children_.emplace_back(std::move(np));
}

void ManagedEnvironment::Create(const fuchsia::sys::EnvironmentPtr& parent,
                                ManagedEnvironment::Options options,
                                const ManagedEnvironment* managed_parent) {
  auto services = EnvironmentServices::Create(parent);

  // Nested environments without a name are not allowed, if empty name is
  // provided, replace it with a default *randomized* value.
  // Randomness there is necessary due to appmgr rules for environments with
  // same name.
  if (!options.has_name() || options.name().empty()) {
    std::random_device rnd;
    options.set_name(fxl::StringPrintf("netemul-env-%08x", rnd()));
  }

  loggers_ = std::make_unique<ManagedLoggerCollection>(options.name());

  // add network context service:
  services->AddService(sandbox_env_->network_context().GetHandler());

  // add Bus service:
  services->AddService(sandbox_env_->sync_manager().GetHandler());

  // add managed environment itself as a handler
  services->AddService(bindings_.GetHandler(this));

  // Inject LogSink service
  services->AddServiceWithLaunchInfo(
      kLogSinkServiceURL,
      [this]() {
        fuchsia::sys::LaunchInfo linfo;
        linfo.url = kLogSinkServiceURL;
        linfo.out = loggers_->CreateLogger(kLogSinkServiceURL, false);
        linfo.err = loggers_->CreateLogger(kLogSinkServiceURL, true);
        loggers_->IncrementCounter();
        return linfo;
      },
      fuchsia::logger::LogSink::Name_);

  // Inject Log service
  services->AddServiceWithLaunchInfo(
      kLogServiceURL,
      [this]() {
        fuchsia::sys::LaunchInfo linfo;
        linfo.url = kLogServiceURL;
        linfo.out = loggers_->CreateLogger(kLogServiceURL, false);
        linfo.err = loggers_->CreateLogger(kLogServiceURL, true);
        loggers_->IncrementCounter();
        return linfo;
      },
      fuchsia::logger::Log::Name_);

  // prepare service configurations:
  service_config_.clear();
  if (options.has_inherit_parent_launch_services() &&
      options.inherit_parent_launch_services() && managed_parent != nullptr) {
    for (const auto& a : managed_parent->service_config_) {
      LaunchService clone;
      a.Clone(&clone);
      service_config_.push_back(std::move(clone));
    }
  }

  if (options.has_services()) {
    std::move(options.mutable_services()->begin(),
              options.mutable_services()->end(),
              std::back_inserter(service_config_));
  }

  // push all the allowable launch services:
  for (const auto& svc : service_config_) {
    LaunchService copy;
    ZX_ASSERT(svc.Clone(&copy) == ZX_OK);
    services->AddServiceWithLaunchInfo(
        svc.url,
        [this, svc = std::move(copy)]() {
          fuchsia::sys::LaunchInfo linfo;
          linfo.url = svc.url;
          linfo.arguments->insert(linfo.arguments->begin(),
                                  svc.arguments->begin(), svc.arguments->end());
          linfo.out = loggers_->CreateLogger(svc.url, false);
          linfo.err = loggers_->CreateLogger(svc.url, true);
          loggers_->IncrementCounter();
          return linfo;
        },
        svc.name);
  }

  if (options.has_devices()) {
    // save all handles for virtual devices
    for (auto& dev : *options.mutable_devices()) {
      virtual_devices_.AddEntry(dev.path, dev.device.Bind());
    }
  }

  fuchsia::sys::EnvironmentOptions sub_options = {
      .kill_on_oom = true,
      .allow_parent_runners = false,
      .inherit_parent_services = false};

  env_ = EnclosingEnvironment::Create(options.name(), parent,
                                      std::move(services), sub_options);

  env_->SetRunningChangedCallback([this](bool running) {
    if (running && running_callback_) {
      running_callback_();
    } else if (!running) {
      FXL_LOG(ERROR) << "Underlying enclosed Environment stopped running";
    }
  });

  launcher_ = std::make_unique<ManagedLauncher>(this);

  // Start LogListener for this environment
  log_listener_ =
      LogListener::Create(this, options.logger_options(), options.name(), NULL);
}

zx::channel ManagedEnvironment::OpenVdevDirectory() {
  return virtual_devices_.OpenAsDirectory();
}

zx::channel ManagedEnvironment::OpenVdataDirectory() {
  if (!virtual_data_) {
    virtual_data_ = std::make_unique<VirtualData>();
  }
  return virtual_data_->GetDirectory();
}

void ManagedEnvironment::Bind(
    fidl::InterfaceRequest<ManagedEnvironment::FManagedEnvironment> req) {
  bindings_.AddBinding(this, std::move(req));
}

ManagedLoggerCollection& ManagedEnvironment::loggers() {
  ZX_ASSERT(loggers_);
  return *loggers_;
}

void ManagedEnvironment::ConnectToService(std::string name, zx::channel req) {
  env_->ConnectToService(name, std::move(req));
}

void ManagedEnvironment::AddDevice(
    fuchsia::netemul::environment::VirtualDevice device) {
  virtual_devices_.AddEntry(device.path, device.device.Bind());
}

void ManagedEnvironment::RemoveDevice(::std::string path) {
  virtual_devices_.RemoveEntry(path);
}

}  // namespace netemul
