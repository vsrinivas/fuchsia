// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/realm.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "src/lib/cmx/cmx.h"
#include "src/lib/cmx/program.h"
#include "src/lib/cmx/runtime.h"
#include "src/lib/cmx/sandbox.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/pkg_url/url_resolver.h"
#include "src/sys/appmgr/constants.h"
#include "src/sys/appmgr/crash_introspector.h"
#include "src/sys/appmgr/dynamic_library_loader.h"
#include "src/sys/appmgr/hub/realm_hub.h"
#include "src/sys/appmgr/moniker.h"
#include "src/sys/appmgr/namespace.h"
#include "src/sys/appmgr/namespace_builder.h"
#include "src/sys/appmgr/policy_checker.h"
#include "src/sys/appmgr/scheme_map.h"
#include "src/sys/appmgr/util.h"

namespace component {

namespace {

constexpr char kAppPath[] = "bin/app";
constexpr char kDataPathPrefix[] = "data/";
constexpr char kDataKey[] = "data";
constexpr char kBinaryKey[] = "binary";
constexpr char kAppArgv0Prefix[] = "/pkg/";
constexpr zx_status_t kComponentCreationFailed = -1;

using fuchsia::sys::TerminationReason;

void PushHandle(uint32_t id, zx_handle_t handle, std::vector<fdio_spawn_action_t>* actions) {
  actions->push_back({.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = id, .handle = handle}});
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

zx::process CreateProcess(const zx::job& job, zx::vmo executable, const std::string& argv0,
                          const std::vector<std::string>& env_vars,
                          fuchsia::sys::LaunchInfo launch_info, zx::channel loader_service,
                          fdio_flat_namespace_t* flat) {
  TRACE_DURATION("appmgr", "Realm::CreateProcess", "launch_info.url", launch_info.url);
  if (!executable)
    return zx::process();

  zx::job duplicate_job;
  zx_status_t status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
  if (status != ZX_OK)
    return zx::process();

  std::string label = Util::GetLabelFromURL(launch_info.url);
  std::vector<const char*> argv{argv0.c_str()};
  if (launch_info.arguments.has_value()) {
    argv.reserve(launch_info.arguments->size() + 2);
    for (const auto& arg : *launch_info.arguments) {
      argv.push_back(arg.c_str());
    }
  }
  argv.push_back(nullptr);

  std::vector<const char*> environ;
  environ.reserve(env_vars.size() + 1);
  for (const auto& env_var : env_vars) {
    environ.push_back(env_var.c_str());
  }
  environ.push_back(nullptr);

  uint32_t flags = FDIO_SPAWN_CLONE_UTC_CLOCK;

  std::vector<fdio_spawn_action_t> actions;

  PushHandle(PA_JOB_DEFAULT, duplicate_job.release(), &actions);

  if (loader_service) {
    PushHandle(PA_LDSVC_LOADER, loader_service.release(), &actions);
  } else {
    // TODO(CP-62): Processes that don't have their own package use the appmgr's
    // dynamic library loader, which doesn't make much sense. We need to find an
    // appropriate loader service for each executable.
    flags |= FDIO_SPAWN_DEFAULT_LDSVC;
  }

  zx::channel directory_request = std::move(launch_info.directory_request);
  if (directory_request) {
    PushHandle(PA_DIRECTORY_REQUEST, directory_request.release(), &actions);
  }

  // TODO(fxbug.dev/49824): Appmgr used to receive a fully-privileged debuglog handle as
  // its stdin, which it would copy to give as a stdin to spawned processes.
  // This handle is mostly useless as a stdin handle, except for the fact that
  // some v1 components assert on being able to clone stdin when creating new
  // processes. Appmgr no longer receives a stdin (or stdout) handle as of
  // fxrev.dev/370683, so as to not break v1 components that assume a valid
  // stdin we clone appmgr's stdin handle which is a closed socket set at
  // startup.
  //
  // Appmgr's stdout handle is populated on startup using
  // StdoutToDebuglog::Init, which installs a write-only debuglog as stdout and
  // stderr, so cloning this handle for new processes gives the same handle
  // (albeit without read rights) as appmgr used to hand out.
  actions.push_back({.action = FDIO_SPAWN_ACTION_CLONE_FD,
                     .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}});
  PushFileDescriptor(std::move(launch_info.out), STDOUT_FILENO, &actions);
  PushFileDescriptor(std::move(launch_info.err), STDERR_FILENO, &actions);

  actions.push_back({.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = label.c_str()}});

  for (size_t i = 0; i < flat->count; ++i) {
    actions.push_back({.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                       .ns = {.prefix = flat->path[i], .handle = flat->handle[i]}});
  }

  executable.set_property(ZX_PROP_NAME, label.data(), label.size());

  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  status = fdio_spawn_vmo(job.get(), flags, executable.release(), argv.data(), environ.data(),
                          actions.size(), actions.data(), process.reset_and_get_address(), err_msg);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot run executable " << label << " due to error " << status << " ("
                   << zx_status_get_string(status) << "): " << err_msg;
  }

  return process;
}

bool IsValidEnvironmentLabel(const std::string& label) {
  static const std::regex* const kEnvironmentLabelRegex = new std::regex{"[0-9a-zA-Z\\.\\-_:#]+"};

  // The regex technically covers the empty check, but checking separately
  // allows us to print a more useful error message.
  if (label.empty()) {
    FX_LOGS(ERROR) << "Environment label cannot be empty";
    return false;
  }
  if (!std::regex_match(label, *kEnvironmentLabelRegex)) {
    FX_LOGS(ERROR) << "Environment label '" << label << "' contains invalid characters";
    return false;
  }
  if (label == "." || label == "..") {
    FX_LOGS(ERROR) << "Environment label cannot be '.' or '..'";
    return false;
  }
  return true;
}

// Returns a unique ID for the component containing all of the 'stable' pieces
// of the component URL, i.e. the repo/host name, package name, variant, and
// resource path but not the package hash/version. This ID is used as a
// filesystem path component.
std::string ComponentUrlToPathComponent(const FuchsiaPkgUrl& fp) {
  // If the parsed URL did not include a resource path, the default is used.
  // TODO(fxbug.dev/4053): Remove this default once all component URLs include a
  // resource path.
  std::string resource = fp.resource_path();
  if (resource.empty()) {
    resource = fp.GetDefaultComponentCmxPath();
  }
  std::replace(resource.begin(), resource.end(), '/', ':');
  return fxl::Substitute("$0:$1:$2#$3", fp.host_name(), fp.package_name(), fp.variant(), resource);
}
}  // namespace

// static
RealmArgs RealmArgs::Make(fxl::WeakPtr<Realm> parent, std::string label, std::string data_path,
                          std::string cache_path, std::string temp_path,
                          const std::shared_ptr<sys::ServiceDirectory>& env_services,
                          bool run_virtual_console, fuchsia::sys::EnvironmentOptions options,
                          fxl::UniqueFD appmgr_config_dir,
                          fbl::RefPtr<ComponentIdIndex> component_id_index) {
  return {.parent = parent,
          .label = label,
          .data_path = data_path,
          .cache_path = cache_path,
          .temp_path = temp_path,
          .environment_services = env_services,
          .run_virtual_console = run_virtual_console,
          .additional_services = nullptr,
          .options = std::move(options),
          .appmgr_config_dir = std::move(appmgr_config_dir),
          .component_id_index = std::move(component_id_index),
          .loader = std::nullopt};
}

RealmArgs RealmArgs::MakeWithAdditionalServices(
    fxl::WeakPtr<Realm> parent, std::string label, std::string data_path, std::string cache_path,
    std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
    bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
    fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir,
    fbl::RefPtr<ComponentIdIndex> component_id_index) {
  return {.parent = parent,
          .label = label,
          .data_path = data_path,
          .cache_path = cache_path,
          .temp_path = temp_path,
          .environment_services = env_services,
          .run_virtual_console = run_virtual_console,
          .additional_services = std::move(additional_services),
          .options = std::move(options),
          .appmgr_config_dir = std::move(appmgr_config_dir),
          .component_id_index = std::move(component_id_index),
          .loader = std::nullopt};
}

RealmArgs RealmArgs::MakeWithCustomLoader(
    fxl::WeakPtr<Realm> parent, std::string label, std::string data_path, std::string cache_path,
    std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
    bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
    fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir,
    fbl::RefPtr<ComponentIdIndex> component_id_index, fuchsia::sys::LoaderPtr loader) {
  return {.parent = parent,
          .label = label,
          .data_path = data_path,
          .cache_path = cache_path,
          .temp_path = temp_path,
          .environment_services = env_services,
          .run_virtual_console = run_virtual_console,
          .additional_services = std::move(additional_services),
          .options = std::move(options),
          .appmgr_config_dir = std::move(appmgr_config_dir),
          .component_id_index = std::move(component_id_index),
          .loader = std::optional<fuchsia::sys::LoaderPtr>{std::move(loader)}};
}

std::unique_ptr<Realm> Realm::Create(RealmArgs args) {
  if (args.label.empty()) {
    FX_LOGS(ERROR) << "Cannot create realm with empty label";
    return nullptr;
  }

  // parent_ is null if this is the root application environment. if so, we
  // derive from the application manager's job.
  zx::unowned<zx::job> parent_job;
  if (args.parent) {
    parent_job = zx::unowned<zx::job>(args.parent->job_);
  } else {
    parent_job = zx::unowned<zx::job>(zx::job::default_job());
  }

  zx::job job;
  auto status = zx::job::create(*parent_job, 0u, &job);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Job creation failed (" << zx_status_get_string(status)
                   << "). Cannot create realm '" << args.label << "'";
    return nullptr;
  }

  return std::make_unique<Realm>(std::move(args), std::move(job));
}

Realm* GetRootRealm(Realm* r) {
  for (; r->parent(); r = r->parent().get()) {
  }
  return r;
}

Realm::Realm(RealmArgs args, zx::job job)
    : parent_(args.parent),
      data_path_(args.data_path),
      cache_path_(args.cache_path),
      temp_path_(args.temp_path),
      run_virtual_console_(args.run_virtual_console),
      job_(std::move(job)),
      hub_(fbl::AdoptRef(new fs::PseudoDir())),
      info_vfs_(async_get_default_dispatcher()),
      environment_services_(args.environment_services),
      appmgr_config_dir_(std::move(args.appmgr_config_dir)),
      use_parent_runners_(args.options.use_parent_runners),
      delete_storage_on_death_(args.options.delete_storage_on_death),
      cpu_watcher_(args.cpu_watcher),
      component_id_index_(std::move(args.component_id_index)),
      weak_ptr_factory_(this) {
  // Only need to create this channel for the root realm.
  if (!parent_) {
    auto status =
        zx::channel::create(0, &first_nested_realm_svc_server_, &first_nested_realm_svc_client_);
    FX_CHECK(status == ZX_OK) << "Cannot create channel: " << zx_status_get_string(status);
  }

  koid_ = std::to_string(fsl::GetKoid(job_.get()));

  label_ = args.label.substr(0, fuchsia::sys::kLabelMaxLength);

  if (parent_) {
    log_connector_ = parent_->log_connector_->NewChild(label_);
  } else {
    log_connector_ = AdoptRef(new LogConnectorImpl(label_));
  }

  if (args.options.kill_on_oom) {
    size_t property_value = 1;
    job_.set_property(ZX_PROP_JOB_KILL_ON_OOM, &property_value, sizeof(property_value));
  }

  if (args.options.inherit_parent_services && parent_->default_namespace_) {
    default_namespace_ = Namespace::CreateChildNamespace(
        parent_->default_namespace_, weak_ptr(), std::move(args.additional_services), nullptr);
  } else {
    default_namespace_ =
        fxl::MakeRefCounted<Namespace>(weak_ptr(), std::move(args.additional_services), nullptr);
  }

  fsl::SetObjectName(job_.get(), label_);
  hub_.SetName(label_);
  hub_.SetJobId(koid_);
  hub_.AddServices(default_namespace_->services());
  hub_.AddJobProvider(fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    default_namespace_->job_provider()->AddBinding(
        fidl::InterfaceRequest<fuchsia::sys::JobProvider>(std::move(channel)));
    return ZX_OK;
  })));

  // Add default services hosted by appmgr for the root realm only.
  if (!parent_) {
    // Set up Loader service for root realm.
    package_loader_.reset(new component::PackageLoader);
    default_namespace_->services()->AddService(
        fuchsia::sys::Loader::Name_, fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          package_loader_->AddBinding(
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
          return ZX_OK;
        })));

    // Set up CacheControl service for root realm.
    cache_control_.reset(new component::CacheControl);
    default_namespace_->services()->AddService(
        fuchsia::sys::test::CacheControl::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          cache_control_->AddBinding(
              fidl::InterfaceRequest<fuchsia::sys::test::CacheControl>(std::move(channel)));
          return ZX_OK;
        })));

    crash_introspector_ = std::make_unique<CrashIntrospector>();
    default_namespace_->services()->AddService(
        fuchsia::sys::internal::CrashIntrospect::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          crash_introspector_->AddBinding(
              fidl::InterfaceRequest<fuchsia::sys::internal::CrashIntrospect>(std::move(channel)));
          return ZX_OK;
        })));
  }

  if (args.loader) {
    loader_ = std::move(args.loader.value());
  } else {
    fuchsia::sys::ServiceProviderPtr service_provider;
    default_namespace_->services()->AddBinding(service_provider.NewRequest());
    service_provider->ConnectToService(fuchsia::sys::Loader::Name_,
                                       loader_.NewRequest().TakeChannel());
  }

  std::string error;
  if (!files::IsDirectoryAt(appmgr_config_dir_.get(), SchemeMap::kConfigDirPath)) {
    FX_LOGS(FATAL) << "Could not find scheme map config dir: " << SchemeMap::kConfigDirPath;
  }
  if (!scheme_map_.ParseFromDirectoryAt(appmgr_config_dir_, SchemeMap::kConfigDirPath)) {
    FX_LOGS(FATAL) << "Could not parse scheme map config dir: " << scheme_map_.error_str();
  }
}

Realm::~Realm() {
  job_.kill();

  ShutdownNamespace();

  if (delete_storage_on_death_) {
    if (!files::DeletePath(data_path(), true)) {
      FX_LOGS(ERROR) << "Failed to delete persistent storage for environment '" << label()
                     << "' on death";
    }
    if (!files::DeletePath(cache_path(), true)) {
      FX_LOGS(ERROR) << "Failed to delete cache storage for environment '" << label()
                     << "' on death";
    }
  }
}

zx::channel Realm::OpenInfoDir() { return Util::OpenAsDirectory(&info_vfs_, hub_dir()); }

HubInfo Realm::HubInfo() { return component::HubInfo(label_, koid_, hub_.dir()); }

zx::job Realm::DuplicateJobForHub() const {
  zx::job duplicate_job;
  // As this only goes inside /hub, it is fine to give destoy rights
  auto flags = ZX_RIGHTS_BASIC | ZX_RIGHT_DESTROY | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_ENUMERATE;
  zx_status_t status = job_.duplicate(flags | ZX_RIGHT_WRITE, &duplicate_job);
  if (status == ZX_ERR_INVALID_ARGS) {
    // In the process of removing WRITE for processes; if duplicate with WRITE
    // failed, try the new rights. TODO(fxbug.dev/32803): Once the transition is
    // complete, only duplicate with MANAGE_PROCESS.
    status = job_.duplicate(flags | ZX_RIGHT_MANAGE_PROCESS, &duplicate_job);
  }
  if (status != ZX_OK) {
    return zx::job();
  }
  return duplicate_job;
}

void Realm::CreateNestedEnvironment(
    fidl::InterfaceRequest<fuchsia::sys::Environment> environment,
    fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> controller_request,
    std::string label, fuchsia::sys::ServiceListPtr additional_services,
    fuchsia::sys::EnvironmentOptions options) {
  TRACE_DURATION("appmgr", "Realm::CreateNestedEnvironment", "label", label);

  // Check that label is valid and unique among existing children.
  if (!IsValidEnvironmentLabel(label)) {
    environment.Close(ZX_ERR_INVALID_ARGS);
    controller_request.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  for (const auto& child : children_) {
    if (label == child.first->label_) {
      FX_LOGS(ERROR) << "Attempt to create nested environment '" << label << "' under '" << label_
                     << "' but label matches existing environment";
      environment.Close(ZX_ERR_BAD_STATE);
      controller_request.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }

  if (additional_services && !additional_services->host_directory) {
    FX_LOGS(ERROR) << label << ": |additional_services.provider| is not supported for "
                   << "CreateNestedEnvironment. Use "
                   << "|additional_services.host_directory| instead.";
    environment.Close(ZX_ERR_INVALID_ARGS);
    controller_request.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  RealmArgs args;
  std::string nested_data_path = files::JoinPath(data_path(), "r/" + label);
  std::string nested_cache_path = files::JoinPath(cache_path(), "r/" + label);
  std::string nested_temp_path = files::JoinPath(temp_path(), "r/" + label);
  if (additional_services) {
    args = RealmArgs::MakeWithAdditionalServices(
        weak_ptr(), label, nested_data_path, nested_cache_path, nested_temp_path,
        environment_services_,
        /*run_virtual_console=*/false, std::move(additional_services), std::move(options),
        appmgr_config_dir_.duplicate(), component_id_index_);
  } else {
    args = RealmArgs::Make(weak_ptr(), label, nested_data_path, nested_cache_path, nested_temp_path,
                           environment_services_,
                           /*run_virtual_console=*/false, std::move(options),
                           appmgr_config_dir_.duplicate(), component_id_index_);
  }
  args.cpu_watcher = cpu_watcher_;

  auto realm = Realm::Create(std::move(args));
  if (!realm) {
    return;
  }

  auto controller =
      std::make_unique<EnvironmentControllerImpl>(std::move(controller_request), std::move(realm));
  Realm* child = controller->realm();
  child->AddBinding(std::move(environment));

  // update hub
  hub_.AddRealm(child->HubInfo());

  // If this is the first nested realm created in the root realm, serve the
  // child realm's service directory on this channel so that
  // BindFirstNestedRealmSvc can be used to connect to it.
  if (!parent_ && children_.empty()) {
    child->default_namespace_->ServeServiceDirectory(std::move(first_nested_realm_svc_server_));
  }

  controller->OnCreated();
  children_.emplace(child, std::move(controller));

  if (run_virtual_console_) {
    // TODO(anmittal): remove svc hardcoding once we no longer need to launch
    // shell with sysmgr services, i.e once we have chrealm.
    CreateShell("/boot/bin/run-vc", child->default_namespace_->OpenServicesAsDirectory());
    CreateShell("/boot/bin/run-vc", child->default_namespace_->OpenServicesAsDirectory());
    CreateShell("/boot/bin/run-vc", child->default_namespace_->OpenServicesAsDirectory());
  }
}

void Realm::Resolve(fidl::StringPtr name, fuchsia::process::Resolver::ResolveCallback callback) {
  TRACE_DURATION("appmgr", "Realm::ResolveLoader", "name", name.value_or(""));

  zx::vmo binary;
  fidl::InterfaceHandle<fuchsia::ldsvc::Loader> loader;

  if (name->empty()) {
    FX_LOGS(ERROR) << "Cannot resolve loader because requested name is empty";
    callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
    return;
  }

  // XXX(raggi): canonicalize url doesn't clean out invalid url chars or fail on
  // them (e.g. \n)
  const std::string canon_url = CanonicalizeURL(name.value_or(""));
  if (canon_url.empty()) {
    FX_LOGS(ERROR) << "Cannot resolve " << name << " because the url could not be canonicalized";
    callback(ZX_ERR_INVALID_ARGS, std::move(binary), std::move(loader));
    return;
  }
  std::string scheme = GetSchemeFromURL(canon_url);

  const std::string launcher_type = scheme_map_.LookUp(scheme);
  if (launcher_type != "package") {
    FX_LOGS(ERROR) << "Cannot resolve non-packages";
    callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
    return;
  }

  FuchsiaPkgUrl pkg_url;
  if (!pkg_url.Parse(canon_url)) {
    FX_LOGS(ERROR) << "Cannot load " << canon_url << " because the URL is not valid.";
    callback(ZX_ERR_INVALID_ARGS, std::move(binary), std::move(loader));
    return;
  }

  auto trace_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("appmgr", "Realm::ResolveLoader::LoadUrl", trace_id, "url", canon_url);
  loader_->LoadUrl(canon_url, [trace_id, callback = std::move(callback),
                               pkg_url](fuchsia::sys::PackagePtr package) mutable {
    TRACE_ASYNC_END("appmgr", "Realm::ResolveLoader::LoadUrl", trace_id);

    zx::vmo binary;
    fidl::InterfaceHandle<fuchsia::ldsvc::Loader> loader;
    if (!package) {
      callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
      return;
    }
    if (!package->data) {
      callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
      return;
    }
    if (!package->directory) {
      callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
      return;
    }
    fbl::unique_fd dirfd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

    // The package loader itself is not expected to give us an executable VMO at
    // package->data, but it is expected to give us a directory handle that is
    // capable of opening other children with OPEN_RIGHT_EXECUTABLE.
    // Get the executably-mappable ELF VMO out of the package directory.

    // Open the resource_path() out of the directory.
    uint32_t flags = fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE;
    fbl::unique_fd exec_fd;
    zx_status_t status = fdio_open_fd_at(dirfd.get(), pkg_url.resource_path().c_str(), flags,
                                         exec_fd.reset_and_get_address());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "fdio_open_fd_at(" << dirfd.get() << ", " << pkg_url.resource_path().c_str()
                     << ", " << flags << ") failed: " << zx_status_get_string(status);
      callback(status, std::move(binary), std::move(loader));
      return;
    }

    // Get the executable VMO.
    status = fdio_get_vmo_exec(exec_fd.get(), binary.reset_and_get_address());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "fdio_get_vmo_exec() failed: " << zx_status_get_string(status);
      callback(status, std::move(binary), std::move(loader));
      return;
    }

    // Start up the library loader.
    zx::status<zx::channel> chan =
        DynamicLibraryLoader::Start(dirfd.get(), Util::GetLabelFromURL(package->resolved_url));
    if (chan.is_error()) {
      callback(chan.status_value(), std::move(binary), std::move(loader));
      return;
    }
    loader.set_channel(std::move(chan).value());
    callback(ZX_OK, std::move(binary), std::move(loader));
  });
}

void Realm::CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                            fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
                            ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateComponent", "launch_info.url", launch_info.url);
  ComponentRequestWrapper component_request(std::move(controller));

  if (launch_info.url.empty()) {
    FX_LOGS(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::URL_INVALID);
    return;
  }

  std::string canon_url = CanonicalizeURL(launch_info.url);
  if (canon_url.empty()) {
    FX_LOGS(ERROR) << "Cannot run " << launch_info.url
                   << " because the url could not be canonicalized";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::URL_INVALID);
    return;
  }
  launch_info.url = canon_url;
  std::string scheme = GetSchemeFromURL(canon_url);

  const std::string launcher_type = scheme_map_.LookUp(scheme);
  if (launcher_type == "") {
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::URL_INVALID);
  } else if (launcher_type == "package") {
    // "package" type doesn't use a runner.

    // launch_info is moved before LoadUrl() gets at its first argument.
    auto lu_trace_id = TRACE_NONCE();
    TRACE_ASYNC_BEGIN("appmgr", "Realm::CreateComponent::LoadUrl", lu_trace_id, "url", canon_url);
    std::string url = launch_info.url;
    loader_->LoadUrl(
        url, [this, lu_trace_id, launch_info = std::move(launch_info),
              component_request = std::move(component_request),
              callback = std::move(callback)](fuchsia::sys::PackagePtr package) mutable {
          TRACE_ASYNC_END("appmgr", "Realm::CreateComponent::LoadUrl", lu_trace_id);

          if (package && package->directory) {
            CreateComponentFromPackage(std::move(package), std::move(launch_info),
                                       std::move(component_request), std::move(callback));
          } else {
            component_request.SetReturnValues(kComponentCreationFailed,
                                              TerminationReason::PACKAGE_NOT_FOUND);
          }
        });
  } else {
    // Component from scheme that maps to a runner.
    CreateComponentWithRunnerForScheme(launcher_type, std::move(launch_info),
                                       std::move(component_request), std::move(callback));
  }
}

Moniker Realm::ComputeMoniker(Realm* realm, const FuchsiaPkgUrl& fp) {
  std::vector<std::string> realm_path;
  for (Realm* leaf = realm; leaf != nullptr; leaf = leaf->parent().get()) {
    realm_path.push_back(leaf->label());
  }
  std::reverse(realm_path.begin(), realm_path.end());
  return Moniker{.url = fp.ToString(), .realm_path = std::move(realm_path)};
}

void Realm::CreateShell(const std::string& path, zx::channel svc) {
  TRACE_DURATION("appmgr", "Realm::CreateShell", "path", path);
  if (!svc)
    return;

  SandboxMetadata sandbox;
  sandbox.AddFeature("deprecated-shell");

  NamespaceBuilder builder = NamespaceBuilder(appmgr_config_dir_.duplicate(), path);
  builder.AddServices(std::move(svc));
  builder.AddSandbox(sandbox, [this] { return OpenInfoDir(); });

  zx_status_t status;
  zx::vmo executable;
  fbl::unique_fd fd;
  status = fdio_open_fd(path.c_str(),
                        fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                        fd.reset_and_get_address());
  if (status != ZX_OK) {
    return;
  }
  status = fdio_get_vmo_exec(fd.get(), executable.reset_and_get_address());
  if (status != ZX_OK) {
    return;
  }

  zx::job child_job;
  status = zx::job::create(job_, 0u, &child_job);
  if (status != ZX_OK)
    return;

  std::vector<std::string> env_vars;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = path;
  zx::process process = CreateProcess(child_job, std::move(executable), path, env_vars,
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

std::shared_ptr<ComponentControllerImpl> Realm::ExtractComponent(
    ComponentControllerImpl* controller) {
  auto it = applications_.find(controller);
  if (it == applications_.end()) {
    return nullptr;
  }
  auto application = std::move(it->second);

  NotifyComponentStopped(application->url(), application->label(), application->hub_instance_id());

  // update hub
  hub_.RemoveComponent(application->HubInfo());

  applications_.erase(it);
  return application;
}

void Realm::AddBinding(fidl::InterfaceRequest<fuchsia::sys::Environment> environment) {
  default_namespace_->AddBinding(std::move(environment));
}

void Realm::CreateComponentWithRunnerForScheme(std::string runner_url,
                                               fuchsia::sys::LaunchInfo launch_info,
                                               ComponentRequestWrapper component_request,
                                               ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateComponentWithRunnerForScheme", "runner_url", runner_url,
                 "launch_info.url", launch_info.url);

  fuchsia::sys::Package package;
  package.resolved_url = launch_info.url;

  fuchsia::sys::StartupInfo startup_info;
  startup_info.launch_info = std::move(launch_info);
  NamespaceBuilder builder =
      NamespaceBuilder(appmgr_config_dir_.duplicate(), startup_info.launch_info.url);
  startup_info.flat_namespace = builder.BuildForRunner();

  auto* runner = GetOrCreateRunner(runner_url);
  if (runner == nullptr) {
    FX_LOGS(ERROR) << "Cannot create " << runner_url << " to run " << startup_info.launch_info.url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::RUNNER_FAILED);
    return;
  }

  fxl::RefPtr<Namespace> ns =
      Namespace::CreateChildNamespace(default_namespace_, weak_ptr(), nullptr, nullptr);

  if (ns.get() == nullptr) {
    component_request.SetReturnValues(-1, fuchsia::sys::TerminationReason::UNSUPPORTED);
    return;
  }

  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
  component_request.Extract(&controller);
  runner->StartComponent(std::move(package), std::move(startup_info), std::move(ns),
                         std::move(controller), std::nullopt);
}

void Realm::CreateComponentFromPackage(fuchsia::sys::PackagePtr package,
                                       fuchsia::sys::LaunchInfo launch_info,
                                       ComponentRequestWrapper component_request,
                                       ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateComponentFromPackage", "package.resolved_url",
                 package->resolved_url, "launch_info.url", launch_info.url);
  fbl::unique_fd pkg_fd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

  // Parse cmx manifest file, if it's there.
  CmxMetadata cmx;
  std::string cmx_path;
  FuchsiaPkgUrl fp;
  bool is_fuchsia_pkg_url = false;
  if (fp.Parse(package->resolved_url)) {
    if (!fp.resource_path().empty()) {
      // If the url has a resource, assume that's the cmx.
      cmx_path = fp.resource_path();

      // The URL is fuchsia-pkg iff it has a resource.
      // TODO(fxbug.dev/4053): Remove this logic once all URLs are fuchsia-pkg.
      is_fuchsia_pkg_url = true;
    } else {
      // It's possible the url does not have a resource, in which case either
      // the cmx exists at meta/<package_name.cmx> or it does not exist.
      cmx_path = fp.GetDefaultComponentCmxPath();
    }
  } else {
    FX_LOGS(ERROR) << "invalid component url: " << package->resolved_url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }
  TRACE_DURATION_BEGIN("appmgr", "Realm::CreateComponentFromPackage:IsFileAt", "cmx_path",
                       cmx_path);
  if (!cmx_path.empty() && files::IsFileAt(pkg_fd.get(), cmx_path)) {
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:IsFileAt");
    json::JSONParser json_parser;
    {
      TRACE_DURATION("appmgr", "Realm::CreateComponentFromPackage:ParseFromFileAt", "cmx_path",
                     cmx_path);
      if (!cmx.ParseFromFileAt(pkg_fd.get(), cmx_path, &json_parser)) {
        FX_LOGS(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
        component_request.SetReturnValues(kComponentCreationFailed,
                                          TerminationReason::INTERNAL_ERROR);
        return;
      }
    }
  } else {
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:IsFileAt");
    FX_LOGS(ERROR) << "Component " << package->resolved_url
                   << " does not have a component manifest (a.k.a. cmx file)! "
                   << "Please add a cmx file to your component. "
                   << "https://fuchsia.dev/fuchsia-src/concepts/storage/"
                   << "package_metadata#component_manifest";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  if (!is_fuchsia_pkg_url) {
    FX_LOGS(ERROR) << "Component could not be launched from " << package->resolved_url
                   << " because it is not a valid Fuchsia component URL!";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  RuntimeMetadata runtime;
  // If meta/*.cmx has runtime data, get it.
  if (!cmx.runtime_meta().IsNull()) {
    runtime = cmx.runtime_meta();
  }

  zx::vmo executable;
  std::string app_argv0;
  fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata;
  const ProgramMetadata& program = cmx.program_meta();

  if (launch_info.arguments.has_value()) {
    launch_info.arguments->insert(launch_info.arguments->begin(), program.args().begin(),
                                  program.args().end());
  } else {
    launch_info.arguments = program.args();
  }

  zx::channel loader_service;
  if (runtime.IsNull()) {
    // If we cannot parse a runtime from either .cmx or deprecated_runtime, then
    // we fall back to the default runner, which is running an ELF binary or
    // shell script.
    const std::string bin_path = program.IsBinaryNull() ? kAppPath : program.binary();

    app_argv0 = fxl::Concatenate({kAppArgv0Prefix, bin_path});
    TRACE_DURATION_BEGIN("appmgr", "Realm::CreateComponentFromPackage:VmoFromFilenameAt",
                         "bin_path", bin_path);
    zx_status_t status;
    fbl::unique_fd elf_fd;
    status = fdio_open_fd_at(pkg_fd.get(), bin_path.c_str(),
                             fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                             elf_fd.reset_and_get_address());
    if (status == ZX_OK) {
      status = fdio_get_vmo_exec(elf_fd.get(), executable.reset_and_get_address());
    }
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:VmoFromFilenameAt");
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to open '" << package->resolved_url << "' program.binary path: '"
                     << bin_path << "', with status: " << status;
      component_request.SetReturnValues(kComponentCreationFailed,
                                        TerminationReason::INTERNAL_ERROR);
      return;
    }

    {
      zx::status<zx::channel> status =
          DynamicLibraryLoader::Start(pkg_fd.get(), Util::GetLabelFromURL(launch_info.url));
      if (status.is_error()) {
        component_request.SetReturnValues(kComponentCreationFailed,
                                          TerminationReason::INTERNAL_ERROR);
        return;
      }
      loader_service = std::move(status).value();
    }
  } else {
    // Read 'data' path from cmx, or assume to be /pkg/data/<component-name>.
    std::string data_path =
        program.IsDataNull() ? kDataPathPrefix + fp.package_name() : program.data();
    // Pass a {"data", "data/<component-name>"} pair through StartupInfo, so
    // components can identify their directory under /pkg/data.
    fuchsia::sys::ProgramMetadata pg;
    pg.key = kDataKey;
    pg.value = data_path;
    program_metadata.emplace({pg});
    // Also add binary path
    if (!program.IsBinaryNull()) {
      pg.key = kBinaryKey;
      pg.value = program.binary();
      program_metadata->push_back(pg);
    }
    // Add in whatever else is in the original specification
    for (const auto& attribute : program.unknown_attributes()) {
      pg.key = attribute.first;
      pg.value = attribute.second;
      program_metadata->push_back(pg);
    }
  }

  // We want two handles to the package, one to put in the component's namespace
  // and one to put in the hub.
  zx::channel pkg = fsl::TransferChannelFromFileDescriptor(std::move(pkg_fd));
  zx::channel pkg_clone;
  if (pkg.is_valid()) {
    pkg_clone = zx::channel(fdio_service_clone(pkg.get()));
  }

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder = NamespaceBuilder(appmgr_config_dir_.duplicate(), fp.ToString());
  builder.AddPackage(std::move(pkg));

  // If meta/*.cmx exists, attempt to read sandbox data from it.
  const std::vector<std::string>* service_allowlist = nullptr;
  std::vector<zx_policy_basic_v2_t> policies;

  if (!cmx.sandbox_meta().IsNull()) {
    const auto& sandbox = cmx.sandbox_meta();
    service_allowlist = &sandbox.services();

    builder.AddConfigData(sandbox, fp.package_name());

    builder.AddSandbox(
        sandbox,
        /*hub_directory_factory=*/[this] { return OpenInfoDir(); },
        /*isolated_data_path_factory=*/
        [&] { return IsolatedPathForComponentInstance(fp, internal::StorageType::DATA); },
        [&] { return IsolatedPathForComponentInstance(fp, internal::StorageType::CACHE); },
        [&] { return IsolatedPathForComponentInstance(fp, internal::StorageType::TEMP); });

    // It is critical that if nothing is returned the component does not lanuch.
    PolicyChecker policy_checker(appmgr_config_dir_.duplicate());
    std::optional<SecurityPolicy> security_policy = policy_checker.Check(sandbox, fp);
    if (!security_policy.has_value()) {
      component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::UNSUPPORTED);
      return;
    }

    if (!security_policy->enable_ambient_executable) {
      policies.push_back(zx_policy_basic_v2_t{.condition = ZX_POL_AMBIENT_MARK_VMO_EXEC,
                                              .action = ZX_POL_ACTION_DENY,
                                              .flags = ZX_POL_OVERRIDE_DENY});
    }

    fxl::RefPtr<Namespace> ns = Namespace::CreateChildNamespace(
        default_namespace_, weak_ptr(), std::move(launch_info.additional_services),
        service_allowlist);

    if (ns.get() == nullptr) {
      component_request.SetReturnValues(-1, fuchsia::sys::TerminationReason::UNSUPPORTED);
      return;
    }

    // Add a component event provider for v1 archivists/observers.
    if (security_policy->enable_component_event_provider) {
      ns->MaybeAddComponentEventProvider();
    }

    ns->set_component_moniker(ComputeMoniker(this, fp));
    zx::channel svc = ns->OpenServicesAsDirectory();
    if (!svc) {
      component_request.SetReturnValues(kComponentCreationFailed,
                                        TerminationReason::INTERNAL_ERROR);
      return;
    }
    builder.AddServices(std::move(svc));

    // Add the custom namespace.
    // Note that this must be the last |builder| step adding entries to the
    // namespace so that we can filter out entries already added in previous
    // steps.
    builder.AddFlatNamespace(std::move(launch_info.flat_namespace));

    if (runtime.IsNull()) {
      // Use the default runner: ELF binaries.
      CreateElfBinaryComponentFromPackage(
          std::move(launch_info), std::move(executable), app_argv0, program.env_vars(),
          std::move(loader_service), builder.Build(), std::move(component_request), std::move(ns),
          policies, std::move(callback), std::move(pkg_clone));
    } else {
      // Use other component runners.
      CreateRunnerComponentFromPackage(std::move(package), std::move(launch_info), runtime,
                                       builder.BuildForRunner(), std::move(component_request),
                                       std::move(ns), std::move(program_metadata),
                                       std::move(pkg_clone));
    }
  }
}

void Realm::CreateElfBinaryComponentFromPackage(
    fuchsia::sys::LaunchInfo launch_info, zx::vmo executable, const std::string& app_argv0,
    const std::vector<std::string>& env_vars, zx::channel loader_service,
    fdio_flat_namespace_t* flat, ComponentRequestWrapper component_request,
    fxl::RefPtr<Namespace> ns, const std::vector<zx_policy_basic_v2_t>& policies,
    ComponentObjectCreatedCallback callback, zx::channel package_handle) {
  TRACE_DURATION("appmgr", "Realm::CreateElfBinaryComponentFromPackage", "launch_info.url",
                 launch_info.url);

  zx::job child_job;
  zx_status_t status = zx::job::create(job_, 0u, &child_job);
  if (status != ZX_OK) {
    return;
  }
  if (!policies.empty()) {
    status = child_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC_V2, policies.data(),
                                  policies.size());
    if (status != ZX_OK) {
      return;
    }
  }

  const std::string args = Util::GetArgsString(launch_info.arguments);
  const std::string url = launch_info.url;  // Keep a copy before moving it.
  auto channels = Util::BindDirectory(&launch_info);
  zx::process process = CreateProcess(child_job, std::move(executable), app_argv0, env_vars,
                                      std::move(launch_info), std::move(loader_service), flat);

  if (process) {
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;

    component_request.Extract(&controller);
    auto application = std::make_shared<ComponentControllerImpl>(
        std::move(controller), this, std::move(child_job), std::move(process), url, std::move(args),
        Util::GetLabelFromURL(url), std::move(ns), std::move(channels.exported_dir),
        std::move(channels.client_request), std::move(package_handle));
    // update hub
    hub_.AddComponent(application->HubInfo());
    ComponentControllerImpl* key = application.get();
    if (callback != nullptr) {
      callback(application);
    }
    fuchsia::sys::internal::SourceIdentity component_info;
    component_info.set_component_name(application->label());
    component_info.set_component_url(application->url());
    component_info.set_instance_id(application->hub_instance_id());
    RegisterJobForCrashIntrospection(application->job(), std::move(component_info));
    NotifyComponentStarted(application->url(), application->label(),
                           application->hub_instance_id());
    applications_.emplace(key, std::move(application));
  }
}

void Realm::CreateRunnerComponentFromPackage(
    fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
    RuntimeMetadata& runtime, fuchsia::sys::FlatNamespace flat,
    ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
    fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata, zx::channel package_handle) {
  TRACE_DURATION("appmgr", "Realm::CreateRunnerComponentFromPackage", "package.resolved_url",
                 package->resolved_url, "launch_info.url", launch_info.url);

  fuchsia::sys::Package inner_package;
  inner_package.resolved_url = package->resolved_url;

  fuchsia::sys::StartupInfo startup_info;
  startup_info.launch_info = std::move(launch_info);
  startup_info.flat_namespace = std::move(flat);
  startup_info.program_metadata = std::move(program_metadata);

  auto* runner = GetOrCreateRunner(runtime.runner());
  if (runner == nullptr) {
    FX_LOGS(ERROR) << "Cannot create " << runner << " to run " << startup_info.launch_info.url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
  component_request.Extract(&controller);
  runner->StartComponent(std::move(inner_package), std::move(startup_info), std::move(ns),
                         std::move(controller), std::move(package_handle));
}

RunnerHolder* Realm::GetOrCreateRunner(const std::string& runner) {
  // Determine the realm whose runner should be used.
  auto realm_runner = GetRunnerRealm();

  auto result = realm_runner->runners_.emplace(runner, nullptr);
  if (result.second) {
    zx::channel request;
    auto runner_services = sys::ServiceDirectory::CreateWithRequest(&request);
    fuchsia::sys::ComponentControllerPtr runner_controller;
    fuchsia::sys::LaunchInfo runner_launch_info;
    runner_launch_info.url = runner;
    runner_launch_info.directory_request = std::move(request);
    result.first->second = std::make_unique<RunnerHolder>(
        std::move(runner_services), std::move(runner_controller), std::move(runner_launch_info),
        realm_runner, [realm_runner, runner] { realm_runner->runners_.erase(runner); });

  } else if (!result.first->second) {
    // There was a cycle in the runner graph.
    FX_LOGS(ERROR) << "Detected a cycle in the runner graph for " << runner << ".";
    return nullptr;
  }

  return result.first->second.get();
}

Realm* Realm::GetRunnerRealm() {
  auto realm = this;

  while (realm->use_parent_runners_ && realm->parent_) {
    realm = realm->parent_.get();
  }

  return realm;
}

zx_status_t Realm::BindFirstNestedRealmSvc(zx::channel channel) {
  if (parent_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fdio_service_clone_to(first_nested_realm_svc_client_.get(), channel.release());
}

// A component instance's storage directory is in one of two places:
//  (a) A directory key'd using component instance ID, if it has one.
//  (b) A directory computed using fn(realm_path, component URL)
//
// If a component is assigned an instance ID while it already has a storage
// directory under (b), its storage directory is moved to (a).
std::string Realm::IsolatedPathForComponentInstance(const FuchsiaPkgUrl& fp,
                                                    internal::StorageType storage_type) {
  // The subdirectory of the root data directory to use persistent storage
  // This applies only to components with an instance ID.
  constexpr char kInstanceIdPersistentSubdir[] = "persistent";

  // Compute directory path based on realm (b).
  std::string old_path;
  switch (storage_type) {
    case internal::StorageType::DATA:
      old_path = files::JoinPath(data_path(), ComponentUrlToPathComponent(fp));
      break;
    case internal::StorageType::CACHE:
      old_path = files::JoinPath(cache_path(), ComponentUrlToPathComponent(fp));
      break;
    case internal::StorageType::TEMP:
      old_path = files::JoinPath(temp_path(), ComponentUrlToPathComponent(fp));
      break;
  };

  std::string path = old_path;
  // if (a) is possible, use it instead, and move (b) to (a) if needed.
  std::string instance_id_path;
  auto instance_id = component_id_index_->LookupMoniker(Realm::ComputeMoniker(this, fp));
  if (instance_id) {
    auto* root_realm = GetRootRealm(this);
    switch (storage_type) {
      case internal::StorageType::DATA:
        instance_id_path =
            files::JoinPath(files::JoinPath(root_realm->data_path(), kInstanceIdPersistentSubdir),
                            instance_id.value());
        break;
      case internal::StorageType::CACHE:
        instance_id_path = files::JoinPath(root_realm->cache_path(), instance_id.value());
        break;
      case internal::StorageType::TEMP:
        instance_id_path = files::JoinPath(root_realm->temp_path(), instance_id.value());
        break;
    };
    path = instance_id_path;

    if (files::IsDirectory(old_path)) {
      if (!files::CreateDirectory(files::GetDirectoryName(instance_id_path)) ||
          rename(old_path.c_str(), instance_id_path.c_str()) != 0) {
        FX_LOGS(ERROR) << "Unable to move component storage directory " << old_path
                       << " to be the new instance ID directory " << instance_id_path
                       << ". errno = " << strerror(errno)
                       << ". Continuing to use moniker based storage directory.";
        path = old_path;
      } else {
        FX_LOGS(INFO) << "Moved component storage directory from " << old_path << " to "
                      << instance_id_path;
      }
    }
  }

  // Ensure directory path exists.
  if (!files::IsDirectory(path) && !files::CreateDirectory(path)) {
    FX_LOGS(ERROR) << "Failed to create data directory " << path;
    return "";
  }

  return path;
}

void Realm::NotifyComponentStarted(const std::string& component_url,
                                   const std::string& component_name,
                                   const std::string& instance_id) {
  auto notify_data = GetEventNotificationInfo(component_url, component_name, instance_id);
  if (notify_data.provider) {
    notify_data.provider->NotifyComponentStarted(std::move(notify_data.component));
  }
}

void Realm::NotifyComponentDiagnosticsDirReady(
    const std::string& component_url, const std::string& component_name,
    const std::string& instance_id, fidl::InterfaceHandle<fuchsia::io::Directory> directory) {
  auto notify_data = GetEventNotificationInfo(component_url, component_name, instance_id);
  if (notify_data.provider) {
    notify_data.provider->NotifyComponentDirReady(std::move(notify_data.component),
                                                  std::move(directory));
  }
}

void Realm::NotifyComponentStopped(const std::string& component_url,
                                   const std::string& component_name,
                                   const std::string& instance_id) {
  auto notify_data = GetEventNotificationInfo(component_url, component_name, instance_id);
  if (notify_data.provider) {
    notify_data.provider->NotifyComponentStopped(std::move(notify_data.component));
  }
}

internal::EventNotificationInfo Realm::GetEventNotificationInfo(const std::string& component_url,
                                                                const std::string& component_name,
                                                                const std::string& instance_id) {
  ComponentEventProviderImpl* provider = nullptr;
  std::vector<std::string> relative_realm_path;

  // If this realm has a ComponentEventProvider, then the relative_realm_path should be empty and
  // the provider attached to this realm should be used.
  if (this->component_event_provider_) {
    provider = this->component_event_provider_.get();
  } else {
    relative_realm_path.push_back(label_);
    auto realm = weak_ptr();

    // Stop traversing the path to the root once a child of the root realm "app" is found.
    while (realm && realm->parent_ && !provider) {
      realm = realm->parent_;
      if (realm->component_event_provider_) {
        provider = realm->component_event_provider_.get();
      } else {
        relative_realm_path.push_back(realm->label_);
      }
    }
    std::reverse(relative_realm_path.begin(), relative_realm_path.end());

    // NOTE: the archivist used to be in the sys realm of the v1 componments world. Now it's a
    // v2 component who is a sibling of appmgr, therefore realm paths (which are relative to
    // the archivist position) will be prefixed by `sys`. To avoid a soft migration of clients
    // depending on the moniker not containing `sys` we strip it. To continue allowing tests using
    // an observer to continue creating environments named "sys" we only strip this prefix if it's
    // the actual sys realm, this is, we stopped at the root realm.
    if (relative_realm_path.size() > 0 && (relative_realm_path[0].compare("sys") == 0) && realm &&
        (realm->label_.compare(internal::kRootLabel) == 0)) {
      relative_realm_path.erase(relative_realm_path.begin());
    }
  }

  fuchsia::sys::internal::SourceIdentity identity;
  identity.set_component_url(component_url);
  identity.set_component_name(component_name);
  identity.set_instance_id(instance_id);
  identity.set_realm_path(relative_realm_path);
  return {.provider = provider, .component = std::move(identity)};
}

zx_status_t Realm::BindComponentEventProvider(
    fidl::InterfaceRequest<fuchsia::sys::internal::ComponentEventProvider> request) {
  if (!component_event_provider_) {
    component_event_provider_ =
        std::make_unique<ComponentEventProviderImpl>(weak_ptr(), async_get_default_dispatcher());
  }
  auto status = component_event_provider_->Connect(std::move(request));
  return status;
}

bool Realm::HasComponentEventListenerBound() {
  return component_event_provider_ && component_event_provider_->listener_bound();
}

void Realm::RegisterJobForCrashIntrospection(
    const zx::job& job, fuchsia::sys::internal::SourceIdentity component_info) {
  component_info.mutable_realm_path()->push_back(label_);
  if (likely(parent_)) {
    parent_->RegisterJobForCrashIntrospection(job, std::move(component_info));
  } else if (crash_introspector_) {
    auto* path = component_info.mutable_realm_path();
    std::reverse(path->begin(), path->end());
    crash_introspector_->RegisterJob(job, std::move(component_info));
  } else {
    FX_LOGS(ERROR) << "Cannot find parent or crash introspector for realm: " << label_;
  }
}

void Realm::ShutdownNamespace(ShutdownNamespaceCallback callback) {
  job_.kill();
  default_namespace_->FlushAndShutdown(default_namespace_, std::move(callback));
}

}  // namespace component
