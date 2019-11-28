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
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <algorithm>
#include <utility>

#include <trace/event.h>

#include "garnet/lib/cmx/cmx.h"
#include "garnet/lib/cmx/program.h"
#include "garnet/lib/cmx/runtime.h"
#include "garnet/lib/cmx/sandbox.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/pkg_url/url_resolver.h"
#include "src/sys/appmgr/allowlist.h"
#include "src/sys/appmgr/dynamic_library_loader.h"
#include "src/sys/appmgr/hub/realm_hub.h"
#include "src/sys/appmgr/namespace_builder.h"
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

constexpr char kDeprecatedShellAllowlist[] =
    "allowlist/deprecated_shell.txt";
constexpr char kDeprecatedAmbientReplaceAsExecAllowlist[] =
    "allowlist/deprecated_ambient_replace_as_executable.txt";
// Delete this when b/140175266 is fixed
constexpr char kOpalTest[] = "opal_test.cmx";

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

  uint32_t flags = 0u;

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
  if (directory_request)
    PushHandle(PA_DIRECTORY_REQUEST, directory_request.release(), &actions);

  PushFileDescriptor(nullptr, STDIN_FILENO, &actions);
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
    FXL_LOG(ERROR) << "Cannot run executable " << label << " due to error " << status << " ("
                   << zx_status_get_string(status) << "): " << err_msg;
  }

  return process;
}

bool IsValidEnvironmentLabel(const std::string& label) {
  static const std::regex* const kEnvironmentLabelRegex = new std::regex{"[0-9a-zA-Z\\.\\-_:#]+"};

  // The regex technically covers the empty check, but checking separately
  // allows us to print a more useful error message.
  if (label.empty()) {
    FXL_LOG(ERROR) << "Environment label cannot be empty";
    return false;
  }
  if (!std::regex_match(label, *kEnvironmentLabelRegex)) {
    FXL_LOG(ERROR) << "Environment label '" << label << "' contains invalid characters";
    return false;
  }
  if (label == "." || label == "..") {
    FXL_LOG(ERROR) << "Environment label cannot be '.' or '..'";
    return false;
  }
  return true;
}

// Returns a unique ID for the component containing all of the 'stable' pieces
// of the component URL, i.e. the repo/host name, package name, variant, and
// resource path but not the package hash/version.
std::string StableComponentID(const FuchsiaPkgUrl& fp) {
  // If the parsed URL did not include a resource path, the default is used.
  // TODO(CF-156): Remove this default once all component URLs include a
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
RealmArgs RealmArgs::Make(Realm* parent, std::string label, std::string data_path,
                          std::string cache_path, std::string temp_path,
                          const std::shared_ptr<sys::ServiceDirectory>& env_services,
                          bool run_virtual_console, fuchsia::sys::EnvironmentOptions options,
                          fxl::UniqueFD appmgr_config_dir) {
  return {.parent = parent,
          .label = label,
          .data_path = data_path,
          .cache_path = cache_path,
          .temp_path = temp_path,
          .environment_services = env_services,
          .run_virtual_console = run_virtual_console,
          .additional_services = nullptr,
          .options = std::move(options),
          .appmgr_config_dir = std::move(appmgr_config_dir)};
}

RealmArgs RealmArgs::MakeWithAdditionalServices(
    Realm* parent, std::string label, std::string data_path, std::string cache_path,
    std::string temp_path, const std::shared_ptr<sys::ServiceDirectory>& env_services,
    bool run_virtual_console, fuchsia::sys::ServiceListPtr additional_services,
    fuchsia::sys::EnvironmentOptions options, fxl::UniqueFD appmgr_config_dir) {
  return {.parent = parent,
          .label = label,
          .data_path = data_path,
          .cache_path = cache_path,
          .temp_path = temp_path,
          .environment_services = env_services,
          .run_virtual_console = run_virtual_console,
          .additional_services = std::move(additional_services),
          .options = std::move(options),
          .appmgr_config_dir = std::move(appmgr_config_dir)};
}

std::unique_ptr<Realm> Realm::Create(RealmArgs args) {
  if (args.label.empty()) {
    FXL_LOG(ERROR) << "Cannot create realm with empty label";
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
    FXL_LOG(ERROR) << "Job creation failed (" << zx_status_get_string(status)
                   << "). Cannot create realm '" << args.label << "'";
    return nullptr;
  }

  return std::make_unique<Realm>(std::move(args), std::move(job));
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
      delete_storage_on_death_(args.options.delete_storage_on_death) {
  // Only need to create this channel for the root realm.
  if (parent_ == nullptr) {
    auto status =
        zx::channel::create(0, &first_nested_realm_svc_server_, &first_nested_realm_svc_client_);
    FXL_CHECK(status == ZX_OK) << "Cannot create channel: " << zx_status_get_string(status);
  }

  koid_ = std::to_string(fsl::GetKoid(job_.get()));
  label_ = args.label.substr(0, fuchsia::sys::kLabelMaxLength);

  if (args.options.kill_on_oom) {
    size_t property_value = 1;
    job_.set_property(ZX_PROP_JOB_KILL_ON_OOM, &property_value, sizeof(property_value));
  }

  if (args.options.inherit_parent_services) {
    default_namespace_ = fxl::MakeRefCounted<Namespace>(
        parent_->default_namespace_, this, std::move(args.additional_services), nullptr);
  } else {
    default_namespace_ =
        fxl::MakeRefCounted<Namespace>(nullptr, this, std::move(args.additional_services), nullptr);
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
  }

  fuchsia::sys::ServiceProviderPtr service_provider;
  default_namespace_->services()->AddBinding(service_provider.NewRequest());
  service_provider->ConnectToService(fuchsia::sys::Loader::Name_,
                                     loader_.NewRequest().TakeChannel());

  std::string error;
  if (!files::IsDirectoryAt(appmgr_config_dir_.get(), SchemeMap::kConfigDirPath)) {
    FXL_LOG(FATAL) << "Could not find scheme map config dir: " << SchemeMap::kConfigDirPath;
  }
  if (!scheme_map_.ParseFromDirectoryAt(appmgr_config_dir_, SchemeMap::kConfigDirPath)) {
    FXL_LOG(FATAL) << "Could not parse scheme map config dir: " << scheme_map_.error_str();
  }
}

Realm::~Realm() {
  job_.kill();

  if (delete_storage_on_death_) {
    if (!files::DeletePath(data_path(), true)) {
      FXL_LOG(ERROR) << "Failed to delete persistent storage for environment '" << label()
                     << "' on death";
    }
    if (!files::DeletePath(cache_path(), true)) {
      FXL_LOG(ERROR) << "Failed to delete cache storage for environment '" << label()
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
    // failed, try the new rights. TODO(ZX-2967): Once the transition is
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
      FXL_LOG(ERROR) << "Attempt to create nested environment '" << label << "' under '" << label_
                     << "' but label matches existing environment";
      environment.Close(ZX_ERR_BAD_STATE);
      controller_request.Close(ZX_ERR_BAD_STATE);
      return;
    }
  }

  if (additional_services && !additional_services->host_directory) {
    FXL_LOG(ERROR) << label << ": |additional_services.provider| is not supported for "
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
        this, label, nested_data_path, nested_cache_path, nested_temp_path, environment_services_,
        /*run_virtual_console=*/false, std::move(additional_services), std::move(options),
        appmgr_config_dir_.duplicate());
  } else {
    args = RealmArgs::Make(this, label, nested_data_path, nested_cache_path, nested_temp_path,
                           environment_services_,
                           /*run_virtual_console=*/false, std::move(options),
                           appmgr_config_dir_.duplicate());
  }

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
  if (parent_ == nullptr && children_.size() == 0) {
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
    FXL_LOG(ERROR) << "Cannot resolve loader because requested name is empty";
    callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
    return;
  }

  // XXX(raggi): canonicalize url doesn't clean out invalid url chars or fail on
  // them (e.g. \n)
  const std::string canon_url = CanonicalizeURL(name.value_or(""));
  if (canon_url.empty()) {
    FXL_LOG(ERROR) << "Cannot resolve " << name << " because the url could not be canonicalized";
    callback(ZX_ERR_INVALID_ARGS, std::move(binary), std::move(loader));
    return;
  }
  std::string scheme = GetSchemeFromURL(canon_url);

  const std::string launcher_type = scheme_map_.LookUp(scheme);
  if (launcher_type != "package") {
    FXL_LOG(ERROR) << "Cannot resolve non-packages";
    callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
    return;
  }

  auto trace_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("appmgr", "Realm::ResolveLoader::LoadUrl", trace_id, "url", canon_url);
  loader_->LoadUrl(canon_url, [trace_id, callback = std::move(callback)](
                                  fuchsia::sys::PackagePtr package) mutable {
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
    binary.swap(package->data->vmo);

    if (!package->directory) {
      callback(ZX_ERR_NOT_FOUND, std::move(binary), std::move(loader));
      return;
    }
    fbl::unique_fd dirfd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

    zx::channel chan;
    if (DynamicLibraryLoader::Start(std::move(dirfd), &chan) != ZX_OK) {
      callback(ZX_ERR_INTERNAL, std::move(binary), std::move(loader));
      return;
    }
    loader.set_channel(std::move(chan));
    callback(ZX_OK, std::move(binary), std::move(loader));
  });
}

void Realm::CreateComponent(fuchsia::sys::LaunchInfo launch_info,
                            fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
                            ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateComponent", "launch_info.url", launch_info.url);
  ComponentRequestWrapper component_request(std::move(controller));

  if (launch_info.url.empty()) {
    FXL_LOG(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::URL_INVALID);
    return;
  }

  std::string canon_url = CanonicalizeURL(launch_info.url);
  if (canon_url.empty()) {
    FXL_LOG(ERROR) << "Cannot run " << launch_info.url
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
  status = fdio_open_fd(path.c_str(), fuchsia::io::OPEN_RIGHT_READABLE |
                        fuchsia::io::OPEN_RIGHT_EXECUTABLE,
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
  NamespaceBuilder builder = NamespaceBuilder(appmgr_config_dir_.duplicate(),
                                              startup_info.launch_info.url);
  startup_info.flat_namespace = builder.BuildForRunner();

  auto* runner = GetOrCreateRunner(runner_url);
  if (runner == nullptr) {
    FXL_LOG(ERROR) << "Cannot create " << runner_url << " to run " << startup_info.launch_info.url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::RUNNER_FAILED);
    return;
  }

  fxl::RefPtr<Namespace> ns =
      fxl::MakeRefCounted<Namespace>(default_namespace_, this, nullptr, nullptr);

  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
  component_request.Extract(&controller);
  runner->StartComponent(std::move(package), std::move(startup_info), std::move(ns),
                         std::move(controller));
}

void Realm::CreateComponentFromPackage(fuchsia::sys::PackagePtr package,
                                       fuchsia::sys::LaunchInfo launch_info,
                                       ComponentRequestWrapper component_request,
                                       ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateComponentFromPackage", "package.resolved_url",
                 package->resolved_url, "launch_info.url", launch_info.url);
  fbl::unique_fd fd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

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
      // TODO(CF-156): Remove this logic once all URLs are fuchsia-pkg.
      is_fuchsia_pkg_url = true;
    } else {
      // It's possible the url does not have a resource, in which case either
      // the cmx exists at meta/<package_name.cmx> or it does not exist.
      cmx_path = fp.GetDefaultComponentCmxPath();
    }
  } else {
    FXL_LOG(ERROR) << "invalid component url: " << package->resolved_url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }
  TRACE_DURATION_BEGIN("appmgr", "Realm::CreateComponentFromPackage:IsFileAt", "cmx_path",
                       cmx_path);
  if (!cmx_path.empty() && files::IsFileAt(fd.get(), cmx_path)) {
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:IsFileAt");
    json::JSONParser json_parser;
    {
      TRACE_DURATION("appmgr", "Realm::CreateComponentFromPackage:ParseFromFileAt", "cmx_path",
                     cmx_path);
      if (!cmx.ParseFromFileAt(fd.get(), cmx_path, &json_parser)) {
        FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
        component_request.SetReturnValues(kComponentCreationFailed,
                                          TerminationReason::INTERNAL_ERROR);
        return;
      }
    }
  } else {
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:IsFileAt");
    FXL_LOG(ERROR) << "Component " << package->resolved_url
                   << " does not have a component manifest (a.k.a. cmx file)! "
                   << "Please add a cmx file to your component. "
                   << "https://fuchsia.dev/fuchsia-src/concepts/storage/"
                   << "package_metadata#component_manifest";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  if (!is_fuchsia_pkg_url) {
    FXL_LOG(ERROR) << "Component could not be launched from " << package->resolved_url
                   << " because it is not a valid Fuchsia component URL!";
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  RuntimeMetadata runtime;
  std::string runtime_parse_error;
  // If meta/*.cmx has runtime data, get it.
  if (!cmx.runtime_meta().IsNull()) {
    runtime = cmx.runtime_meta();
  }

  zx::vmo executable;
  std::string app_argv0;
  fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata;
  const ProgramMetadata program = cmx.program_meta();

  if (launch_info.arguments.has_value()) {
    launch_info.arguments->insert(launch_info.arguments->begin(), program.args().begin(),
                                  program.args().end());
  } else {
    launch_info.arguments = program.args();
  }

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
    status = fdio_open_fd_at(fd.get(), bin_path.c_str(),
                             fuchsia::io::OPEN_RIGHT_READABLE |
                             fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                             elf_fd.reset_and_get_address());
    if (status == ZX_OK) {
        status = fdio_get_vmo_exec(elf_fd.get(), executable.reset_and_get_address());
    }
    TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:VmoFromFilenameAt");
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "component '" << package->resolved_url << "' has neither runner (error: '"
                     << runtime_parse_error << "') nor elf binary: '" << bin_path << "'";
      component_request.SetReturnValues(kComponentCreationFailed,
                                        TerminationReason::INTERNAL_ERROR);
      return;
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
  }

  // TODO(abarth): We shouldn't need to clone the channel here. Instead, we
  // should be able to tear down the file descriptor in a way that gives us
  // the channel back.
  TRACE_DURATION_BEGIN("appmgr",
                       "Realm::CreateComponentFromPackage:CloneChannelFromFileDescriptor");
  zx::channel pkg = fsl::CloneChannelFromFileDescriptor(fd.get());
  TRACE_DURATION_END("appmgr", "Realm::CreateComponentFromPackage:CloneChannelFromFileDescriptor");
  zx::channel loader_service;
  if (DynamicLibraryLoader::Start(std::move(fd), &loader_service) != ZX_OK) {
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder = NamespaceBuilder(appmgr_config_dir_.duplicate(),
                                              fp.ToString());
  builder.AddPackage(std::move(pkg));

  // If meta/*.cmx exists, attempt to read sandbox data from it.
  const std::vector<std::string>* service_whitelist = nullptr;
  std::vector<zx_policy_basic_t> policies;

  bool should_have_ambient_executable = false;
  if (!cmx.sandbox_meta().IsNull()) {
    const auto& sandbox = cmx.sandbox_meta();
    service_whitelist = &sandbox.services();

    builder.AddConfigData(sandbox, fp.package_name());

    builder.AddSandbox(
        sandbox,
        /*hub_directory_factory=*/[this] { return OpenInfoDir(); },
        /*isolated_data_path_factory=*/
        [&] { return IsolatedPathForPackage(data_path(), fp); },
        [&] { return IsolatedPathForPackage(cache_path(), fp); },
        [&] { return IsolatedPathForPackage(temp_path(), fp); });

    if (sandbox.HasFeature("deprecated-ambient-replace-as-executable")) {
      if (!IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(fp.ToString())) {
        FXL_LOG(ERROR) << "Component " << fp.ToString() << " is not allowed to use "
                       << "deprecated-ambient-replace-as-executable. go/fx-hermetic-sandboxes";
        component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::UNSUPPORTED);
        return;
      }
      should_have_ambient_executable = true;
    }

    if (sandbox.HasFeature("deprecated-shell")
        && !IsAllowedToUseDeprecatedShell(fp.ToString())) {
      FXL_LOG(ERROR) << "Component " << fp.ToString() << " is not allowed to use "
                     << "deprecated-shell. go/fx-hermetic-sandboxes";
      component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::UNSUPPORTED);
      return;
    }
  }
  if (!should_have_ambient_executable) {
    policies.push_back(
        zx_policy_basic_t{.condition = ZX_POL_AMBIENT_MARK_VMO_EXEC, .policy = ZX_POL_ACTION_DENY});
  }

  fxl::RefPtr<Namespace> ns = fxl::MakeRefCounted<Namespace>(
      default_namespace_, this, std::move(launch_info.additional_services), service_whitelist);
  ns->set_component_url(launch_info.url);
  zx::channel svc = ns->OpenServicesAsDirectory();
  if (!svc) {
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
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
    CreateElfBinaryComponentFromPackage(std::move(launch_info), std::move(executable), app_argv0,
                                        program.env_vars(), std::move(loader_service),
                                        builder.Build(), std::move(component_request),
                                        std::move(ns), policies, std::move(callback));
  } else {
    // Use other component runners.
    CreateRunnerComponentFromPackage(std::move(package), std::move(launch_info), runtime,
                                     builder.BuildForRunner(), std::move(component_request),
                                     std::move(ns), std::move(program_metadata));
  }
}

void Realm::CreateElfBinaryComponentFromPackage(
    fuchsia::sys::LaunchInfo launch_info, zx::vmo executable, const std::string& app_argv0,
    const std::vector<std::string>& env_vars, zx::channel loader_service,
    fdio_flat_namespace_t* flat, ComponentRequestWrapper component_request,
    fxl::RefPtr<Namespace> ns, const std::vector<zx_policy_basic_t>& policies,
    ComponentObjectCreatedCallback callback) {
  TRACE_DURATION("appmgr", "Realm::CreateElfBinaryComponentFromPackage", "launch_info.url",
                 launch_info.url);

  zx::job child_job;
  zx_status_t status = zx::job::create(job_, 0u, &child_job);
  if (status != ZX_OK) {
    return;
  }
  if (!policies.empty()) {
    status = child_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, policies.data(),
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
        std::move(channels.client_request));
    // update hub
    hub_.AddComponent(application->HubInfo());
    ComponentControllerImpl* key = application.get();
    if (callback != nullptr) {
      callback(application);
    }
    applications_.emplace(key, std::move(application));
  }
}

void Realm::CreateRunnerComponentFromPackage(
    fuchsia::sys::PackagePtr package, fuchsia::sys::LaunchInfo launch_info,
    RuntimeMetadata& runtime, fuchsia::sys::FlatNamespace flat,
    ComponentRequestWrapper component_request, fxl::RefPtr<Namespace> ns,
    fidl::VectorPtr<fuchsia::sys::ProgramMetadata> program_metadata) {
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
    FXL_LOG(ERROR) << "Cannot create " << runner << " to run " << startup_info.launch_info.url;
    component_request.SetReturnValues(kComponentCreationFailed, TerminationReason::INTERNAL_ERROR);
    return;
  }

  fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller;
  component_request.Extract(&controller);
  runner->StartComponent(std::move(inner_package), std::move(startup_info), std::move(ns),
                         std::move(controller));
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
    FXL_LOG(ERROR) << "Detected a cycle in the runner graph for " << runner << ".";
    return nullptr;
  }

  return result.first->second.get();
}

Realm* Realm::GetRunnerRealm() {
  auto realm = this;

  while (realm->use_parent_runners_ && realm->parent_) {
    realm = realm->parent_;
  }

  return realm;
}

zx_status_t Realm::BindFirstNestedRealmSvc(zx::channel channel) {
  if (parent_ != nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fdio_service_clone_to(first_nested_realm_svc_client_.get(), channel.release());
}

std::string Realm::IsolatedPathForPackage(std::string path_prefix, const FuchsiaPkgUrl& fp) {
  // Create a unique path for this combination of Realm and Component
  // identities. The Realm part comes from path_prefix, which should include all
  // Realm labels from the root Realm to this Realm. The Component part comes
  // from combining the Component URL.
  std::string path = files::JoinPath(path_prefix, StableComponentID(fp));
  if (!files::IsDirectory(path) && !files::CreateDirectory(path)) {
    FXL_LOG(ERROR) << "Failed to create data directory " << path;
    return "";
  }
  return path;
}

bool Realm::IsAllowedToUseDeprecatedShell(std::string ns_id) {
  Allowlist deprecated_shell_allowlist(appmgr_config_dir_,
                                       kDeprecatedShellAllowlist,
                                       Allowlist::kExpected);
  if (deprecated_shell_allowlist.IsAllowed(ns_id)) {
    return true;
  }
  // Delete the below when b/140175266 is fixed
  const std::string opal_test = kOpalTest;
  return ns_id.size() >= opal_test.size() &&
         ns_id.compare(ns_id.size() - opal_test.size(), opal_test.size(), opal_test) == 0;
}

bool Realm::IsAllowedToUseDeprecatedAmbientReplaceAsExecutable(std::string ns_id) {
  Allowlist deprecated_exec_allowlist(appmgr_config_dir_,
                                      kDeprecatedAmbientReplaceAsExecAllowlist,
                                      Allowlist::kOptional);
  // We treat the absence of the allowlist as an indication that we should be
  // permissive and allow all components to use replace-as-executable.  We add
  // the allowlist in user builds to ensure we are enforcing policy.
  if (!deprecated_exec_allowlist.WasFilePresent()) {
    return true;
  }
  // Otherwise, enforce the allowlist.
  return deprecated_exec_allowlist.IsAllowed(ns_id);
}


}  // namespace component
