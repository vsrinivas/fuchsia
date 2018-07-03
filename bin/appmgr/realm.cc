// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/realm.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>

#include "garnet/bin/appmgr/cmx_metadata.h"
#include "garnet/bin/appmgr/dynamic_library_loader.h"
#include "garnet/bin/appmgr/hub/realm_hub.h"
#include "garnet/bin/appmgr/namespace_builder.h"
#include "garnet/bin/appmgr/runtime_metadata.h"
#include "garnet/bin/appmgr/sandbox_metadata.h"
#include "garnet/bin/appmgr/url_resolver.h"
#include "garnet/bin/appmgr/util.h"
#include "garnet/lib/far/format.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

constexpr char kNumberedLabelFormat[] = "env-%d";
constexpr char kAppPath[] = "bin/app";
constexpr char kAppArv0[] = "/pkg/bin/app";
constexpr char kLegacyFlatExportedDirPath[] = "meta/legacy_flat_exported_dir";
constexpr char kRuntimePath[] = "meta/runtime";

std::vector<const char*> GetArgv(const std::string& argv0,
                                 const fuchsia::sys::LaunchInfo& launch_info) {
  std::vector<const char*> argv;
  argv.reserve(launch_info.arguments->size() + 2);
  argv.push_back(argv0.c_str());
  for (const auto& argument : *launch_info.arguments)
    argv.push_back(argument->c_str());
  argv.push_back(nullptr);
  return argv;
}

void PushHandle(uint32_t id, zx_handle_t handle,
                std::vector<fdio_spawn_action_t>* actions) {
  actions->push_back({.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
                      .h = {.id = id, .handle = handle}});
}

void PushFileDescriptor(fuchsia::sys::FileDescriptorPtr fd, int target_fd,
                        std::vector<fdio_spawn_action_t>* actions) {
  if (!fd) {
    actions->push_back({.action = FDIO_SPAWN_ACTION_CLONE_FD,
                        .fd = {.local_fd = target_fd, .target_fd = target_fd}});
    return;
  }
  if (fd->type0) {
    uint32_t id = PA_HND(PA_HND_TYPE(fd->type0), target_fd);
    PushHandle(id, fd->handle0.release(), actions);
  }
  if (fd->type1) {
    uint32_t id = PA_HND(PA_HND_TYPE(fd->type1), target_fd);
    PushHandle(id, fd->handle1.release(), actions);
  }
  if (fd->type2) {
    uint32_t id = PA_HND(PA_HND_TYPE(fd->type2), target_fd);
    PushHandle(id, fd->handle2.release(), actions);
  }
}

zx::process CreateProcess(const zx::job& job, fsl::SizedVmo data,
                          const std::string& argv0,
                          fuchsia::sys::LaunchInfo launch_info,
                          zx::channel loader_service,
                          fdio_flat_namespace_t* flat) {
  if (!data)
    return zx::process();

  zx::job duplicate_job;
  zx_status_t status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  if (status != ZX_OK)
    return zx::process();

  std::string label = Util::GetLabelFromURL(launch_info.url);
  std::vector<const char*> argv = GetArgv(argv0, launch_info);

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  uint32_t flags = FDIO_SPAWN_CLONE_ENVIRON;

  std::vector<fdio_spawn_action_t> actions;

  PushHandle(PA_JOB_DEFAULT, duplicate_job.release(), &actions);

  if (loader_service) {
    PushHandle(PA_LDSVC_LOADER, loader_service.release(), &actions);
  } else {
    // TODO(CP-62): Processes that don't have their own package use the appmgr's
    // dynamic library loader, which doesn't make much sense. We need to find an
    // appropriate loader service for each executable.
    flags |= FDIO_SPAWN_CLONE_LDSVC;
  }

  zx::channel directory_request = std::move(launch_info.directory_request);
  if (directory_request)
    PushHandle(PA_DIRECTORY_REQUEST, directory_request.release(), &actions);

  PushFileDescriptor(nullptr, STDIN_FILENO, &actions);
  PushFileDescriptor(std::move(launch_info.out), STDOUT_FILENO, &actions);
  PushFileDescriptor(std::move(launch_info.err), STDERR_FILENO, &actions);

  actions.push_back(
      {.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = label.c_str()}});

  for (size_t i = 0; i < flat->count; ++i) {
    actions.push_back(
        {.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
         .ns = {.prefix = flat->path[i], .handle = flat->handle[i]}});
  }

  data.vmo().set_property(ZX_PROP_NAME, label.data(), label.size());

  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  status = fdio_spawn_vmo(job.get(), flags, data.vmo().release(), argv.data(),
                          nullptr, actions.size(), actions.data(),
                          process.reset_and_get_address(), err_msg);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot run executable " << label << " due to error "
                   << status << " (" << zx_status_get_string(status)
                   << "): " << err_msg;
  }

  return process;
}

}  // namespace

uint32_t Realm::next_numbered_label_ = 1u;

Realm::Realm(RealmArgs args)
    : parent_(args.parent),
      run_virtual_console_(args.run_virtual_console),
      default_namespace_(
          fxl::MakeRefCounted<Namespace>(nullptr, this, nullptr)),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      info_vfs_(async_get_default()) {
  // parent_ is null if this is the root application environment. if so, we
  // derive from the application manager's job.
  zx::unowned<zx::job> parent_job;
  if (parent_) {
    parent_job = zx::unowned<zx::job>(parent_->job_);
  } else {
    parent_job = zx::unowned<zx::job>(zx::job::default_job());
  }

  // init svc service channel for root application environment
  if (parent_ == nullptr) {
    FXL_CHECK(zx::channel::create(0, &svc_channel_server_,
                                  &svc_channel_client_) == ZX_OK);
  }
  FXL_CHECK(zx::job::create(*parent_job, 0u, &job_) == ZX_OK);

  koid_ = std::to_string(fsl::GetKoid(job_.get()));
  if (args.label->size() == 0)
    label_ = fxl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
  else
    label_ = args.label.get().substr(0, fuchsia::sys::kLabelMaxLength);

  fsl::SetObjectName(job_.get(), label_);
  hub_.SetName(label_);
  hub_.SetJobId(koid_);
  hub_.AddServices(default_namespace_->services());

  default_namespace_->services()->set_backing_dir(
      std::move(args.host_directory));

  fuchsia::sys::ServiceProviderPtr service_provider;
  default_namespace_->services()->AddBinding(service_provider.NewRequest());
  loader_ = fuchsia::sys::ConnectToService<fuchsia::sys::Loader>(
      service_provider.get());
}

Realm::~Realm() { job_.kill(); }

zx::channel Realm::OpenInfoDir() {
  return Util::OpenAsDirectory(&info_vfs_, hub_dir());
}

HubInfo Realm::HubInfo() {
  return component::HubInfo(label_, koid_, hub_.dir());
}

void Realm::CreateNestedJob(
    zx::channel host_directory,
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController>
        controller_request,
    fidl::StringPtr label) {
  RealmArgs args{this, std::move(host_directory), label, false};
  auto controller = std::make_unique<EnvironmentControllerImpl>(
      std::move(controller_request), std::make_unique<Realm>(std::move(args)));
  Realm* child = controller->realm();
  child->AddBinding(std::move(environment));

  // update hub
  hub_.AddRealm(child->HubInfo());

  children_.emplace(child, std::move(controller));

  Realm* root_realm = this;
  while (root_realm->parent() != nullptr) {
    root_realm = root_realm->parent();
  }
  child->default_namespace_->ServeServiceDirectory(
      std::move(root_realm->svc_channel_server_));

  if (run_virtual_console_) {
    // TODO(anmittal): remove svc hardcoding once we no longer need to launch
    // shell with sysmgr services, i.e once we have chrealm.
    CreateShell("/boot/bin/run-vc",
                child->default_namespace_->OpenServicesAsDirectory());
    CreateShell("/boot/bin/run-vc",
                child->default_namespace_->OpenServicesAsDirectory());
    CreateShell("/boot/bin/run-vc",
                child->default_namespace_->OpenServicesAsDirectory());
  }
}

void Realm::CreateComponent(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    ComponentObjectCreatedCallback callback) {
  if (launch_info.url.get().empty()) {
    FXL_LOG(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    return;
  }
  std::string canon_url = CanonicalizeURL(launch_info.url);
  if (canon_url.empty()) {
    FXL_LOG(ERROR) << "Cannot run " << launch_info.url
                   << " because the url could not be canonicalized";
    return;
  }
  launch_info.url = canon_url;

  std::string scheme = GetSchemeFromURL(canon_url);

  fxl::RefPtr<Namespace> ns = default_namespace_;
  if (launch_info.additional_services) {
    ns = fxl::MakeRefCounted<Namespace>(
        default_namespace_, this, std::move(launch_info.additional_services));
  }

  // TODO(CP-69): Provision this map as a config file rather than hard-coding.
  if (scheme == "http" || scheme == "https") {
    CreateComponentFromNetwork(std::move(launch_info), std::move(controller),
                               std::move(ns), std::move(callback));
    return;
  }

  // launch_info is moved before LoadComponent() gets at its first argument.
  fidl::StringPtr url = launch_info.url;
  loader_->LoadComponent(
      url, fxl::MakeCopyable([this, launch_info = std::move(launch_info),
                              controller = std::move(controller), ns,
                              callback = fbl::move(callback)](
                                 fuchsia::sys::PackagePtr package) mutable {
        if (package) {
          if (package->data) {
            CreateComponentWithProcess(
                std::move(package), std::move(launch_info),
                std::move(controller), std::move(ns), fbl::move(callback));
          } else if (package->directory) {
            CreateComponentFromPackage(
                std::move(package), std::move(launch_info),
                std::move(controller), std::move(ns), fbl::move(callback));
          }
        }
      }));
}

void Realm::CreateShell(const std::string& path, zx::channel svc) {
  if (!svc)
    return;

  SandboxMetadata sandbox;
  sandbox.AddFeature("shell");

  NamespaceBuilder builder;
  builder.AddServices(std::move(svc));
  builder.AddSandbox(sandbox, [this] { return OpenInfoDir(); });

  fsl::SizedVmo executable;
  if (!fsl::VmoFromFilename(path, &executable))
    return;

  zx::job child_job;
  zx_status_t status = zx::job::create(job_, 0u, &child_job);
  if (status != ZX_OK)
    return;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = path;
  zx::process process =
      CreateProcess(child_job, std::move(executable), path,
                    std::move(launch_info), zx::channel(), builder.Build());
}

std::unique_ptr<EnvironmentControllerImpl> Realm::ExtractChild(Realm* child) {
  auto it = children_.find(child);
  if (it == children_.end()) {
    return nullptr;
  }
  auto controller = std::move(it->second);

  // update hub
  hub_.RemoveRealm(child->HubInfo());

  children_.erase(it);
  return controller;
}

std::unique_ptr<ComponentControllerImpl> Realm::ExtractComponent(
    ComponentControllerImpl* controller) {
  auto it = applications_.find(controller);
  if (it == applications_.end()) {
    return nullptr;
  }
  auto application = std::move(it->second);

  // update hub
  hub_.RemoveComponent(application->HubInfo());

  applications_.erase(it);
  return application;
}

void Realm::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment) {
  default_namespace_->AddBinding(std::move(environment));
}

void Realm::CreateComponentWithProcess(
    fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    fxl::RefPtr<Namespace> ns, ComponentObjectCreatedCallback callback) {
  zx::channel svc = ns->OpenServicesAsDirectory();
  if (!svc)
    return;

  NamespaceBuilder builder;
  builder.AddServices(std::move(svc));

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  // HACK(alhaad): We add deprecated default directories after this.
  builder.AddFlatNamespace(std::move(launch_info.flat_namespace));
  // TODO(abarth): Remove this call to AddDeprecatedDefaultDirectories once
  // every application has a proper sandbox configuration.
  builder.AddDeprecatedDefaultDirectories();

  fsl::SizedVmo executable;
  if (!fsl::SizedVmo::FromTransport(std::move(*package->data), &executable))
    return;

  zx::job child_job;
  zx_status_t status = zx::job::create(job_, 0u, &child_job);
  if (status != ZX_OK)
    return;

  const std::string args = Util::GetArgsString(launch_info.arguments);
  const std::string url = launch_info.url;  // Keep a copy before moving it.
  auto channels = Util::BindDirectory(&launch_info);
  zx::process process =
      CreateProcess(child_job, std::move(executable), url,
                    std::move(launch_info), zx::channel(), builder.Build());

  if (process) {
    auto application = std::make_unique<ComponentControllerImpl>(
        std::move(controller), this, std::move(child_job), std::move(process), url,
        std::move(args), Util::GetLabelFromURL(url), std::move(ns),
        ExportedDirType::kPublicDebugCtrlLayout,
        std::move(channels.exported_dir), std::move(channels.client_request));
    // update hub
    hub_.AddComponent(application->HubInfo());
    ComponentControllerImpl* key = application.get();
    if (callback != nullptr) {
      callback(key);
    }
    applications_.emplace(key, std::move(application));
  }
}

void Realm::CreateComponentFromNetwork(
    fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    fxl::RefPtr<Namespace> ns, ComponentObjectCreatedCallback callback) {
  zx::channel svc = ns->OpenServicesAsDirectory();
  if (!svc)
    return;

  NamespaceBuilder builder;
  builder.AddServices(std::move(svc));

  fuchsia::sys::Package package;
  package.resolved_url = launch_info.url;

  fuchsia::sys::StartupInfo startup_info;
  startup_info.launch_info = std::move(launch_info);
  startup_info.flat_namespace = builder.BuildForRunner();

  // TODO(CP-71): Remove web_runner_prototype scaffolding once there is a real
  // web_runner.
  const char* runner_url = "web_runner_prototype";
  if (!files::IsDirectory("/pkgfs/packages/web_runner_prototype"))
    runner_url = "web_runner";

  auto* runner = GetOrCreateRunner(runner_url);
  if (runner == nullptr) {
    FXL_LOG(ERROR) << "Cannot create " << runner_url << " to run "
                   << launch_info.url;
    return;
  }

  runner->StartComponent(std::move(package), std::move(startup_info),
                         std::move(ns), std::move(controller));
}

void Realm::CreateComponentFromPackage(
    fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    fxl::RefPtr<Namespace> ns, ComponentObjectCreatedCallback callback) {
  zx::channel svc = ns->OpenServicesAsDirectory();
  if (!svc)
    return;

  fxl::UniqueFD fd =
      fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

  std::string cmx_data;
  std::string cmx_path = CmxMetadata::GetCmxPath(package->resolved_url.get());
  if (!cmx_path.empty())
    files::ReadFileToStringAt(fd.get(), cmx_path, &cmx_data);

  std::string runtime_data;
  fsl::SizedVmo app_data;
  if (!files::ReadFileToStringAt(fd.get(), kRuntimePath, &runtime_data))
    VmoFromFilenameAt(fd.get(), kAppPath, &app_data);

  ExportedDirType exported_dir_layout =
      files::IsFileAt(fd.get(), kLegacyFlatExportedDirPath)
          ? ExportedDirType::kLegacyFlatLayout
          : ExportedDirType::kPublicDebugCtrlLayout;
  // TODO(abarth): We shouldn't need to clone the channel here. Instead, we
  // should be able to tear down the file descriptor in a way that gives us
  // the channel back.
  zx::channel pkg = fsl::CloneChannelFromFileDescriptor(fd.get());
  zx::channel loader_service;
  if (DynamicLibraryLoader::Start(std::move(fd), &loader_service) != ZX_OK)
    return;

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder;
  builder.AddPackage(std::move(pkg));
  builder.AddServices(std::move(svc));

  // If meta/*.cmx exists, attempt to read sandbox data from it.
  if (!cmx_data.empty()) {
    SandboxMetadata sandbox;
    CmxMetadata cmx;
    rapidjson::Value sandbox_meta;

    if (cmx.ParseSandboxMetadata(cmx_data, &sandbox_meta)) {
      // If the cmx has a sandbox attribute, but it doesn't properly parse,
      // return early. Otherwise, proceed normally as it just means there is
      // no sandbox data for this component.
      if (!sandbox.Parse(sandbox_meta)) {
        FXL_LOG(ERROR) << "Failed to parse sandbox metadata for "
                       << launch_info.url;
        return;
      }
      // If an app has the "shell" feature, then we use the libraries from the
      // system rather than from the package because programs spawned from the
      // shell will need the system-provided libraries to run.
      if (sandbox.HasFeature("shell"))
        loader_service.reset();

      builder.AddSandbox(sandbox, [this] { return OpenInfoDir(); });
    }
  }

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info.flat_namespace));

  if (app_data) {

    zx::job child_job;
    zx_status_t status = zx::job::create(job_, 0u, &child_job);
    if (status != ZX_OK)
      return;

    const std::string args = Util::GetArgsString(launch_info.arguments);
    const std::string url = launch_info.url;  // Keep a copy before moving it.
    auto channels = Util::BindDirectory(&launch_info);
    zx::process process = CreateProcess(
        child_job, std::move(app_data), kAppArv0, std::move(launch_info),
        std::move(loader_service), builder.Build());

    if (process) {
      auto application = std::make_unique<ComponentControllerImpl>(
          std::move(controller), this, std::move(child_job), std::move(process), url,
          std::move(args), Util::GetLabelFromURL(url), std::move(ns),
          exported_dir_layout, std::move(channels.exported_dir),
          std::move(channels.client_request));
      // update hub
      hub_.AddComponent(application->HubInfo());
      ComponentControllerImpl* key = application.get();
      if (callback != nullptr) {
        callback(key);
      }
      applications_.emplace(key, std::move(application));
    }
  } else {
    RuntimeMetadata runtime;

    // If meta/*.cmx exists, read runtime data from it.
    if (!cmx_data.empty()) {
      if (!runtime.Parse(cmx_data)) {
        if (!runtime.Parse(runtime_data)) {
          // If meta/*.cmx has no runtime data, fallback to the *package*'s
          // meta/runtime.
          FXL_LOG(ERROR) << "Failed to parse runtime metadata for "
                         << launch_info.url;
          return;
        }
      }
    } else if (!runtime.Parse(runtime_data)) {
      // If meta/*.cmx has no runtime data, fallback to the *package*'s
      // meta/runtime.
      FXL_LOG(ERROR) << "Failed to parse runtime metadata for "
                     << launch_info.url;
      return;
    }

    fuchsia::sys::Package inner_package;
    inner_package.resolved_url = package->resolved_url;

    fuchsia::sys::StartupInfo startup_info;
    startup_info.launch_info = std::move(launch_info);
    startup_info.flat_namespace = builder.BuildForRunner();

    auto* runner = GetOrCreateRunner(runtime.runner());
    if (runner == nullptr) {
      FXL_LOG(ERROR) << "Cannot create " << runner << " to run "
                     << launch_info.url;
      return;
    }
    runner->StartComponent(std::move(inner_package), std::move(startup_info),
                           std::move(ns), std::move(controller));
  }
}

RunnerHolder* Realm::GetOrCreateRunner(const std::string& runner) {
  // We create the entry in |runners_| before calling ourselves
  // recursively to detect cycles.
  auto result = runners_.emplace(runner, nullptr);
  if (result.second) {
    fuchsia::sys::Services runner_services;
    fuchsia::sys::ComponentControllerPtr runner_controller;
    fuchsia::sys::LaunchInfo runner_launch_info;
    runner_launch_info.url = runner;
    runner_launch_info.directory_request = runner_services.NewRequest();
    result.first->second = std::make_unique<RunnerHolder>(
        std::move(runner_services), std::move(runner_controller),
        std::move(runner_launch_info), this,
        [this, runner] { runners_.erase(runner); });

  } else if (!result.first->second) {
    // There was a cycle in the runner graph.
    FXL_LOG(ERROR) << "Detected a cycle in the runner graph for " << runner
                   << ".";
    return nullptr;
  }

  return result.first->second.get();
}

zx_status_t Realm::BindSvc(zx::channel channel) {
  Realm* root_realm = this;
  while (root_realm->parent() != nullptr) {
    root_realm = root_realm->parent();
  }
  return fdio_service_clone_to(root_realm->svc_channel_client_.get(),
                               channel.release());
}

}  // namespace component
