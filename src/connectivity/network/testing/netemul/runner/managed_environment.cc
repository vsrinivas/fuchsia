// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "managed_environment.h"

#include <fuchsia/boot/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>

#include <random>

#include <sdk/lib/sys/cpp/termination_reason.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace netemul {

namespace {
// Start the Log and LogSink service (the same component publishses both
// services))
constexpr const char* kLogServiceURLNoKlog =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx";
constexpr const char* kLogServiceURLWithKlog =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-with-klog.cmx";
constexpr const char* kLogServices[] = {fuchsia::logger::LogSink::Name_,
                                        fuchsia::logger::Log::Name_,
                                        fuchsia::diagnostics::ArchiveAccessor::Name_};
}  // namespace

using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;

ManagedEnvironment::~ManagedEnvironment() {
  // The environment should be torn down before anything its child components may depend on
  // (i.e. loggers, virtual devices, etc.).
  //
  // Tearing down the loggers before we tear down the environment may cause a rust component
  // to panic when it tries to write to stdout/stderr after their sockets have been closed
  // (https://github.com/rust-lang/rust/blob/f6072ca/src/libstd/io/stdio.rs#L878).
  env_ = nullptr;

  // Always destroy loggers next so they can consume all outstanding messages.
  loggers_ = nullptr;
}

ManagedEnvironment::Ptr ManagedEnvironment::CreateRoot(const fuchsia::sys::EnvironmentPtr& parent,
                                                       const SandboxEnv::Ptr& sandbox_env,
                                                       Options options) {
  auto ret = ManagedEnvironment::Ptr(new ManagedEnvironment(sandbox_env));
  ret->Create(parent, std::move(options));
  return ret;
}

ManagedEnvironment::ManagedEnvironment(const SandboxEnv::Ptr& sandbox_env)
    : sandbox_env_(sandbox_env), ready_(false) {}

sys::testing::EnclosingEnvironment& ManagedEnvironment::environment() { return *env_; }

void ManagedEnvironment::GetLauncher(::fidl::InterfaceRequest<::fuchsia::sys::Launcher> launcher) {
  launcher_->Bind(std::move(launcher));
}

void ManagedEnvironment::CreateChildEnvironment(fidl::InterfaceRequest<FManagedEnvironment> me,
                                                Options options) {
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
  // Nested environments without a name are not allowed, if empty name is
  // provided, replace it with a default *randomized* value.
  // Randomness there is necessary due to appmgr rules for environments with
  // same name.
  if (!options.has_name() || options.name().empty()) {
    std::random_device rnd;
    options.set_name(fxl::StringPrintf("netemul-env-%08x", rnd()));
  }

  // Start LogListener for this environment
  log_listener_ =
      LogListener::Create(std::move(*options.mutable_logger_options()), options.name(), nullptr);

  auto services = EnvironmentServices::Create(parent);

  services->SetServiceTerminatedCallback([this, name = options.name()](
                                             const std::string& service, int64_t exit_code,
                                             fuchsia::sys::TerminationReason reason) {
    FX_LOGS(WARNING) << "Service " << service << " exited on environment " << name << " with ("
                     << exit_code << ") reason: " << sys::HumanReadableTerminationReason(reason);
    if (sandbox_env_->events().service_terminated) {
      sandbox_env_->events().service_terminated(service, exit_code, reason);
    }
  });

  if (log_listener_) {
    loggers_ = std::make_unique<ManagedLoggerCollection>(options.name(),
                                                         log_listener_->GetLogListenerImpl());
  } else {
    loggers_ = std::make_unique<ManagedLoggerCollection>(options.name(), nullptr);
  }

  // Add network context service.
  services->AddService(sandbox_env_->network_context().GetHandler());
  // Add Bus service.
  services->AddService(sandbox_env_->sync_manager().GetHandler());
  // Add managed environment itself as a handler.
  services->AddService(bindings_.GetHandler(this));
  // Add shared tun service.
  services->AddService(fidl::InterfaceRequestHandler<fuchsia::net::tun::Control>(
      [this](fidl::InterfaceRequest<fuchsia::net::tun::Control> request) {
        sandbox_env_->ConnectNetworkTun(std::move(request));
      }));

  bool enable_klog = LogListener::IsKlogsEnabled(options);
  if (enable_klog) {
    services->AddService(fidl::InterfaceRequestHandler<fuchsia::boot::ReadOnlyLog>(
        [this](fidl::InterfaceRequest<fuchsia::boot::ReadOnlyLog> request) {
          // Connect the sandbox to our namespace rather than its sandbox parent.
          sandbox_env_->ConnectToReadOnlyLog(std::move(request));
        }));
  }

  const char* log_service_url;
  if (enable_klog) {
    log_service_url = kLogServiceURLWithKlog;
  } else {
    log_service_url = kLogServiceURLNoKlog;
  }

  // Inject all services provided by LogService.
  for (const auto* svc : kLogServices) {
    // Inject Log service
    services->AddServiceWithLaunchInfo(
        log_service_url,
        [this, log_service_url]() {
          fuchsia::sys::LaunchInfo linfo;
          linfo.url = log_service_url;
          linfo.out = loggers_->CreateLogger(log_service_url, false);
          linfo.err = loggers_->CreateLogger(log_service_url, true);
          loggers_->IncrementCounter();
          return linfo;
        },
        svc);
  }

  // Allow sysinfo service
  services->AllowParentService(fuchsia::sysinfo::SysInfo::Name_);

  // prepare service configurations:
  service_config_.clear();
  if (options.has_inherit_parent_launch_services() && options.inherit_parent_launch_services() &&
      managed_parent != nullptr) {
    for (const auto& a : managed_parent->service_config_) {
      LaunchService clone;
      a.Clone(&clone);
      service_config_.push_back(std::move(clone));
    }
  }

  if (options.has_services()) {
    std::move(options.mutable_services()->begin(), options.mutable_services()->end(),
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
          linfo.arguments = svc.arguments;

          if (!launcher_->MakeServiceLaunchInfo(&linfo)) {
            // NOTE: we can just log an return code of MakeServiceLaunchInfo here, since those are
            // caused by fuchsia::sys::Loader errors that will happen again once we return the
            // launch info. That failure, in turn, will be caught by the service termination
            // callback installed in the services instance.
            FX_LOGS(ERROR) << "Make service launch info failed";
          }
          return linfo;
        },
        svc.name);
  }

  if (auto guest = sandbox_env_->guest_env_.lock()) {
    services->AddService<fuchsia::netemul::guest::GuestDiscovery>(
        [guest = std::move(guest)](
            fidl::InterfaceRequest<fuchsia::netemul::guest::GuestDiscovery> request) {
          guest->ConnectToService(fuchsia::netemul::guest::GuestDiscovery::Name_,
                                  request.TakeChannel());
        });
  }

  if (options.has_devices()) {
    // save all handles for virtual devices
    for (auto& dev : *options.mutable_devices()) {
      virtual_devices_.AddEntry(dev.path, dev.device.Bind());
    }
  }

  fuchsia::sys::EnvironmentOptions sub_options = {.inherit_parent_services = false,
                                                  .use_parent_runners = false,
                                                  .kill_on_oom = true,
                                                  .delete_storage_on_death = true};

  env_ = EnclosingEnvironment::Create(options.name(), parent, std::move(services), sub_options);

  env_->SetRunningChangedCallback([this](bool running) {
    ready_ = true;
    if (running) {
      for (auto& r : pending_requests_) {
        Bind(std::move(r));
      }
      pending_requests_.clear();
      if (running_callback_) {
        running_callback_();
      }
    } else {
      FX_LOGS(ERROR) << "Underlying enclosed Environment stopped running";
      running_callback_ = nullptr;
      children_.clear();
      pending_requests_.clear();
      env_ = nullptr;
      launcher_ = nullptr;
      bindings_.CloseAll();
    }
  });

  launcher_ = std::make_unique<ManagedLauncher>(this);

  // If we have one, bind our log listener to this environment.
  // We do this after creation of log listener because
  // we need to make sure the environment is created first,
  // but managed logger needs our implementation of LogListenerImpl.
  if (log_listener_) {
    ZX_ASSERT(log_listener_->Bindable());
    log_listener_->BindToLogService(this);
  }
}

zx::channel ManagedEnvironment::OpenVdevDirectory(std::string path) {
  return virtual_devices_.OpenAsDirectory(std::move(path));
}

void ManagedEnvironment::Bind(fidl::InterfaceRequest<ManagedEnvironment::FManagedEnvironment> req) {
  if (ready_) {
    bindings_.AddBinding(this, std::move(req));
  } else if (env_) {
    pending_requests_.push_back(std::move(req));
  } else {
    req.Close(ZX_ERR_INTERNAL);
  }
}

ManagedLoggerCollection& ManagedEnvironment::loggers() {
  ZX_ASSERT(loggers_);
  return *loggers_;
}

void ManagedEnvironment::ConnectToService(std::string name, zx::channel req) {
  env_->ConnectToService(name, std::move(req));
}

void ManagedEnvironment::AddDevice(fuchsia::netemul::environment::VirtualDevice device) {
  virtual_devices_.AddEntry(std::move(device.path), device.device.Bind());
}

void ManagedEnvironment::RemoveDevice(std::string path) {
  virtual_devices_.RemoveEntry(std::move(path));
}

}  // namespace netemul
