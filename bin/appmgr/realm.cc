// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/realm.h"

#include <fcntl.h>
#include <fdio/namespace.h>
#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <lib/async/default.h>
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
#include "garnet/lib/far/format.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

constexpr zx_rights_t kChildJobRights = ZX_RIGHTS_BASIC | ZX_RIGHTS_IO;

constexpr char kNumberedLabelFormat[] = "env-%d";
constexpr char kAppPath[] = "bin/app";
constexpr char kAppArv0[] = "/pkg/bin/app";
constexpr char kLegacyFlatExportedDirPath[] = "meta/legacy_flat_exported_dir";
constexpr char kRuntimePath[] = "meta/runtime";
constexpr char kSandboxPath[] = "meta/sandbox";

enum class LaunchType {
  kProcess,
  kArchive,
};

std::vector<const char*> GetArgv(const std::string& argv0,
                                 const LaunchInfo& launch_info) {
  std::vector<const char*> argv;
  argv.reserve(launch_info.arguments->size() + 1);
  argv.push_back(argv0.c_str());
  for (const auto& argument : *launch_info.arguments)
    argv.push_back(argument.get().c_str());
  return argv;
}

std::string GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

void PushFileDescriptor(component::FileDescriptorPtr fd, int new_fd,
                        std::vector<uint32_t>* ids,
                        std::vector<zx_handle_t>* handles) {
  if (!fd)
    return;
  if (fd->type0) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type0), new_fd));
    handles->push_back(fd->handle0.release());
  }
  if (fd->type1) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type1), new_fd));
    handles->push_back(fd->handle1.release());
  }
  if (fd->type2) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type2), new_fd));
    handles->push_back(fd->handle2.release());
  }
}

zx::process CreateProcess(const zx::job& job, fsl::SizedVmo data,
                          const std::string& argv0, LaunchInfo launch_info,
                          zx::channel loader_service,
                          fdio_flat_namespace_t* flat) {
  if (!data)
    return zx::process();

  std::string label = GetLabelFromURL(launch_info.url);
  std::vector<const char*> argv = GetArgv(argv0, launch_info);

  std::vector<uint32_t> ids;
  std::vector<zx_handle_t> handles;

  zx::channel directory_request = std::move(launch_info.directory_request);
  if (directory_request) {
    ids.push_back(PA_DIRECTORY_REQUEST);
    handles.push_back(directory_request.release());
  }

  PushFileDescriptor(std::move(launch_info.out), STDOUT_FILENO, &ids, &handles);
  PushFileDescriptor(std::move(launch_info.err), STDERR_FILENO, &ids, &handles);

  for (size_t i = 0; i < flat->count; ++i) {
    ids.push_back(flat->type[i]);
    handles.push_back(flat->handle[i]);
  }

  data.vmo().set_property(ZX_PROP_NAME, label.data(), label.size());

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  launchpad_t* lp = nullptr;
  launchpad_create(job.get(), label.c_str(), &lp);

  launchpad_clone(lp, LP_CLONE_ENVIRON);
  launchpad_clone_fd(lp, STDIN_FILENO, STDIN_FILENO);
  if (!launch_info.out)
    launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
  if (!launch_info.err)
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);
  if (loader_service)
    launchpad_use_loader_service(lp, loader_service.release());
  launchpad_set_args(lp, argv.size(), argv.data());
  launchpad_set_nametable(lp, flat->count, flat->path);
  launchpad_add_handles(lp, handles.size(), handles.data(), ids.data());
  launchpad_load_from_vmo(lp, data.vmo().release());

  zx_handle_t proc;
  const char* errmsg;
  zx_handle_t status = launchpad_go(lp, &proc, &errmsg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot run executable " << label << " due to error "
                   << status << " (" << zx_status_get_string(status)
                   << "): " << errmsg;
    return zx::process();
  }
  return zx::process(proc);
}

LaunchType Classify(const zx::vmo& data, std::string* runner) {
  if (!data)
    return LaunchType::kProcess;
  std::string magic(archive::kMagicLength, '\0');
  zx_status_t status = data.read(&magic[0], 0, magic.length());
  if (status != ZX_OK)
    return LaunchType::kProcess;
  if (memcmp(magic.data(), &archive::kMagic, sizeof(archive::kMagic)) == 0)
    return LaunchType::kArchive;
  return LaunchType::kProcess;
}

struct ExportedDirChannels {
  // The client side of the channel serving connected application's exported
  // dir.
  zx::channel exported_dir;

  // The server side of our client's |LaunchInfo.directory_request|.
  zx::channel client_request;
};

ExportedDirChannels BindDirectory(LaunchInfo* launch_info) {
  zx::channel exported_dir_server, exported_dir_client;
  zx_status_t status =
      zx::channel::create(0u, &exported_dir_server, &exported_dir_client);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel for service directory: status="
                   << status;
    return {zx::channel(), zx::channel()};
  }

  auto client_request = std::move(launch_info->directory_request);
  launch_info->directory_request = std::move(exported_dir_server);
  return {std::move(exported_dir_client), std::move(client_request)};
}

std::string GetArgsString(
    const ::fidl::VectorPtr<::fidl::StringPtr>& arguments) {
  std::string args = "";
  if (!arguments->empty()) {
    std::ostringstream buf;
    std::copy(arguments->begin(), arguments->end() - 1,
              std::ostream_iterator<std::string>(buf, " "));
    buf << *arguments->rbegin();
    args = buf.str();
  }
  return args;
}

}  // namespace

uint32_t Realm::next_numbered_label_ = 1u;

Realm::Realm(Realm* parent, zx::channel host_directory, fidl::StringPtr label)
    : parent_(parent),
      default_namespace_(
          fxl::MakeRefCounted<Namespace>(nullptr, this, nullptr)),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      info_vfs_(async_get_default()) {
  // parent_ is null if this is the root application environment. if so, we
  // derive from the application manager's job.
  zx_handle_t parent_job =
      parent_ != nullptr ? parent_->job_.get() : zx_job_default();

  // init svc service channel for root application environment
  if (parent_ == nullptr) {
    FXL_CHECK(zx::channel::create(0, &svc_channel_server_,
                                  &svc_channel_client_) == ZX_OK);
  }
  FXL_CHECK(zx::job::create(parent_job, 0u, &job_) == ZX_OK);
  FXL_CHECK(job_.duplicate(kChildJobRights, &job_for_child_) == ZX_OK);

  koid_ = std::to_string(fsl::GetKoid(job_.get()));
  if (label->size() == 0)
    label_ = fxl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
  else
    label_ = label.get().substr(0, component::kLabelMaxLength);

  fsl::SetObjectName(job_.get(), label_);
  hub_.SetName(label_);
  hub_.SetJobId(koid_);

  default_namespace_->services().set_backing_dir(std::move(host_directory));

  ServiceProviderPtr service_provider;
  default_namespace_->services().AddBinding(service_provider.NewRequest());
  loader_ = ConnectToService<Loader>(service_provider.get());
}

Realm::~Realm() { job_.kill(); }

zx::channel Realm::OpenRootInfoDir() {
  // TODO: define /hub access policy: CP-26
  Realm* root_realm = this;
  while (root_realm->parent() != nullptr) {
    root_realm = root_realm->parent();
  }
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    return zx::channel();
  }

  if (info_vfs_.ServeDirectory(root_realm->hub_dir(), std::move(h1)) != ZX_OK) {
    return zx::channel();
  }
  return h2;
}

HubInfo Realm::HubInfo() {
  return component::HubInfo(label_, koid_, hub_.dir());
}

void Realm::CreateNestedJob(
    zx::channel host_directory, fidl::InterfaceRequest<Environment> environment,
    fidl::InterfaceRequest<EnvironmentController> controller_request,
    fidl::StringPtr label) {
  auto controller = std::make_unique<EnvironmentControllerImpl>(
      std::move(controller_request),
      std::make_unique<Realm>(this, std::move(host_directory), label));
  Realm* child = controller->realm();
  child->AddBinding(std::move(environment));

  // update hub
  hub_.AddRealm(child->HubInfo());

  children_.emplace(child, std::move(controller));

  Realm* root_realm = this;
  while (root_realm->parent() != nullptr) {
    root_realm = root_realm->parent();
  }
  child->default_namespace_->services().ServeDirectory(
      std::move(root_realm->svc_channel_server_));

  if (!parent()) {
    child->CreateShell("/boot/bin/run-vc");
    child->CreateShell("/boot/bin/run-vc");
    child->CreateShell("/boot/bin/run-vc");
  }
}

void Realm::CreateApplication(
    LaunchInfo launch_info,
    fidl::InterfaceRequest<ComponentController> controller) {
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

  // launch_info is moved before LoadComponent() gets at its first argument.
  fidl::StringPtr url = launch_info.url;
  loader_->LoadComponent(
      url, fxl::MakeCopyable([this, launch_info = std::move(launch_info),
                              controller = std::move(controller)](
                                 PackagePtr package) mutable {
        fxl::RefPtr<Namespace> ns = default_namespace_;
        if (launch_info.additional_services) {
          ns = fxl::MakeRefCounted<Namespace>(
              default_namespace_, this,
              std::move(launch_info.additional_services));
        }

        if (package) {
          if (package->data) {
            std::string runner;
            LaunchType type = Classify(package->data->vmo, &runner);
            switch (type) {
              case LaunchType::kProcess:
                CreateApplicationWithProcess(
                    std::move(package), std::move(launch_info),
                    std::move(controller), std::move(ns));
                break;
              case LaunchType::kArchive:
                CreateApplicationFromPackage(
                    std::move(package), std::move(launch_info),
                    std::move(controller), std::move(ns));
                break;
            }
          } else if (package->directory) {
            CreateApplicationFromPackage(std::move(package),
                                         std::move(launch_info),
                                         std::move(controller), std::move(ns));
          }
        }
      }));
}

void Realm::CreateShell(const std::string& path) {
  zx::channel svc = default_namespace_->services().OpenAsDirectory();
  if (!svc)
    return;

  SandboxMetadata sandbox;
  sandbox.AddFeature("shell");

  NamespaceBuilder builder;
  builder.AddServices(std::move(svc));
  builder.AddSandbox(sandbox, [this] { return OpenRootInfoDir(); });

  fsl::SizedVmo executable;
  if (!fsl::VmoFromFilename(path, &executable))
    return;

  LaunchInfo launch_info;
  launch_info.url = path;
  zx::process process =
      CreateProcess(job_for_child_, std::move(executable), path,
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

std::unique_ptr<ComponentControllerImpl> Realm::ExtractApplication(
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

void Realm::AddBinding(fidl::InterfaceRequest<Environment> environment) {
  default_namespace_->AddBinding(std::move(environment));
}

void Realm::CreateApplicationWithProcess(
    PackagePtr package, LaunchInfo launch_info,
    fidl::InterfaceRequest<ComponentController> controller,
    fxl::RefPtr<Namespace> ns) {
  zx::channel svc = ns->services().OpenAsDirectory();
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

  const std::string args = GetArgsString(launch_info.arguments);
  const std::string url = launch_info.url;  // Keep a copy before moving it.
  auto channels = BindDirectory(&launch_info);
  zx::process process =
      CreateProcess(job_for_child_, std::move(executable), url,
                    std::move(launch_info), zx::channel(), builder.Build());

  if (process) {
    auto application = std::make_unique<ComponentControllerImpl>(
        std::move(controller), this, nullptr, std::move(process), url,
        std::move(args), GetLabelFromURL(url), std::move(ns),
        ExportedDirType::kPublicDebugCtrlLayout,
        std::move(channels.exported_dir), std::move(channels.client_request));
    // update hub
    hub_.AddComponent(application->HubInfo());
    ComponentControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

void Realm::CreateApplicationFromPackage(
    PackagePtr package, LaunchInfo launch_info,
    fidl::InterfaceRequest<ComponentController> controller,
    fxl::RefPtr<Namespace> ns) {
  zx::channel svc = ns->services().OpenAsDirectory();
  if (!svc)
    return;

  zx::channel pkg;
  std::unique_ptr<archive::FileSystem> pkg_fs;
  std::string cmx_data;
  std::string cmx_path = CmxMetadata::GetCmxPath(package->resolved_url.get());
  std::string sandbox_data;
  std::string runtime_data;
  ExportedDirType exported_dir_layout(ExportedDirType::kPublicDebugCtrlLayout);
  fsl::SizedVmo app_data;
  zx::channel loader_service;

  if (package->data) {
    pkg_fs =
        std::make_unique<archive::FileSystem>(std::move(package->data->vmo));
    pkg = pkg_fs->OpenAsDirectory();
    pkg_fs->GetFileAsString(cmx_path, &cmx_data);
    pkg_fs->GetFileAsString(kSandboxPath, &sandbox_data);
    if (!pkg_fs->GetFileAsString(kRuntimePath, &runtime_data))
      app_data = pkg_fs->GetFileAsVMO(kAppPath);
    exported_dir_layout = pkg_fs->IsFile(kLegacyFlatExportedDirPath)
                              ? ExportedDirType::kLegacyFlatLayout
                              : ExportedDirType::kPublicDebugCtrlLayout;
  } else if (package->directory) {
    fxl::UniqueFD fd =
        fsl::OpenChannelAsFileDescriptor(std::move(package->directory));
    if (!cmx_path.empty()) {
      files::ReadFileToStringAt(fd.get(), cmx_path, &cmx_data);
    }
    files::ReadFileToStringAt(fd.get(), kSandboxPath, &sandbox_data);
    if (!files::ReadFileToStringAt(fd.get(), kRuntimePath, &runtime_data))
      VmoFromFilenameAt(fd.get(), kAppPath, &app_data);
    exported_dir_layout = files::IsFileAt(fd.get(), kLegacyFlatExportedDirPath)
                              ? ExportedDirType::kLegacyFlatLayout
                              : ExportedDirType::kPublicDebugCtrlLayout;
    // TODO(abarth): We shouldn't need to clone the channel here. Instead, we
    // should be able to tear down the file descriptor in a way that gives us
    // the channel back.
    pkg = fsl::CloneChannelFromFileDescriptor(fd.get());
    if (DynamicLibraryLoader::Start(std::move(fd), &loader_service) != ZX_OK)
      return;
  }
  if (!pkg)
    return;

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder;
  builder.AddPackage(std::move(pkg));
  builder.AddServices(std::move(svc));

  // If meta/*.cmx exists, read sandbox data from it, instead of meta/sandbox.
  // TODO(CP-37): Remove meta/sandbox once completely migrated.
  if (!sandbox_data.empty() || !cmx_data.empty()) {
    SandboxMetadata sandbox;

    CmxMetadata cmx;
    if (!cmx_data.empty()) {
      sandbox.Parse(cmx.ParseSandboxMetadata(cmx_data));
    } else {
      if (!sandbox.Parse(sandbox_data)) {
        FXL_LOG(ERROR) << "Failed to parse sandbox metadata for "
                       << launch_info.url;
        return;
      }
    }

    // If an app has the "shell" feature, then we use the libraries from the
    // system rather than from the package because programs spawned from the
    // shell will need the system-provided libraries to run.
    if (sandbox.HasFeature("shell"))
      loader_service.reset();

    builder.AddSandbox(sandbox, [this] { return OpenRootInfoDir(); });
  }

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info.flat_namespace));

  if (app_data) {
    const std::string args = GetArgsString(launch_info.arguments);
    const std::string url = launch_info.url;  // Keep a copy before moving it.
    auto channels = BindDirectory(&launch_info);
    zx::process process = CreateProcess(
        job_for_child_, std::move(app_data), kAppArv0, std::move(launch_info),
        std::move(loader_service), builder.Build());

    if (process) {
      auto application = std::make_unique<ComponentControllerImpl>(
          std::move(controller), this, std::move(pkg_fs), std::move(process),
          url, std::move(args), GetLabelFromURL(url), std::move(ns),
          exported_dir_layout, std::move(channels.exported_dir),
          std::move(channels.client_request));
      // update hub
      hub_.AddComponent(application->HubInfo());
      ComponentControllerImpl* key = application.get();
      applications_.emplace(key, std::move(application));
    }
  } else {
    RuntimeMetadata runtime;
    if (!runtime.Parse(runtime_data)) {
      FXL_LOG(ERROR) << "Failed to parse runtime metadata for "
                     << launch_info.url;
      return;
    }

    Package inner_package;
    inner_package.resolved_url = package->resolved_url;

    StartupInfo startup_info;
    startup_info.launch_info = std::move(launch_info);
    startup_info.flat_namespace = builder.BuildForRunner();

    auto* runner = GetOrCreateRunner(runtime.runner());
    if (runner == nullptr) {
      FXL_LOG(ERROR) << "Cannot create " << runner << " to run "
                     << launch_info.url;
      return;
    }
    runner->StartComponent(std::move(inner_package), std::move(startup_info),
                           std::move(pkg_fs), std::move(ns),
                           std::move(controller));
  }
}

RunnerHolder* Realm::GetOrCreateRunner(const std::string& runner) {
  // We create the entry in |runners_| before calling ourselves
  // recursively to detect cycles.
  auto result = runners_.emplace(runner, nullptr);
  if (result.second) {
    Services runner_services;
    ComponentControllerPtr runner_controller;
    LaunchInfo runner_launch_info;
    runner_launch_info.url = runner;
    runner_launch_info.directory_request = runner_services.NewRequest();
    CreateApplication(std::move(runner_launch_info),
                      runner_controller.NewRequest());

    runner_controller.set_error_handler(
        [this, runner] { runners_.erase(runner); });

    result.first->second = std::make_unique<RunnerHolder>(
        std::move(runner_services), std::move(runner_controller));
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
