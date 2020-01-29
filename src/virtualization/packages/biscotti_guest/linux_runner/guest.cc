// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/packages/biscotti_guest/linux_runner/guest.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/vector.h>
#include <netinet/in.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <memory>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/bin/vmm/guest_config.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/packages/biscotti_guest/linux_runner/ports.h"
#include "src/virtualization/packages/biscotti_guest/third_party/protos/vm_guest.grpc.pb.h"

#include <grpc++/grpc++.h>
#include <grpc++/server_posix.h>

namespace linux_runner {

static constexpr const char* kLinuxEnvirionmentName = "biscotti";
static constexpr const char* kLinuxGuestPackage =
    "fuchsia-pkg://fuchsia.com/biscotti_guest#meta/biscotti_guest.cmx";
static constexpr const char* kContainerName = "stretch";
static constexpr const char* kContainerImageAlias = "debian/stretch";
static constexpr const char* kContainerImageServer =
    "https://storage.googleapis.com/cros-containers";
static constexpr const char* kDefaultContainerUser = "machina";
static constexpr const char* kLinuxUriScheme = "linux://";

// Minfs max file size is currently just under 4GB.
static constexpr const char* kStatefulImagePath = "/data/stateful.img";
static constexpr const char* kExtrasImagePath = "/pkg/data/extras.img";

static fidl::InterfaceHandle<fuchsia::io::File> GetOrCreateStatefulPartition(size_t image_size) {
  TRACE_DURATION("linux_runner", "GetOrCreateStatefulPartition");
  int fd = open(kStatefulImagePath, O_RDWR);
  if (fd < 0 && errno == ENOENT) {
    fd = open(kStatefulImagePath, O_RDWR | O_CREAT);
    if (fd < 0) {
      FXL_LOG(ERROR) << "Failed to create stateful image: " << strerror(errno);
      return nullptr;
    }
    if (ftruncate(fd, image_size) < 0) {
      FXL_LOG(ERROR) << "Failed to truncate image: " << strerror(errno);
      return nullptr;
    }
  }
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open image: " << strerror(errno);
    return nullptr;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_get_service_handle(fd, &handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get service handle: " << status;
    return nullptr;
  }
  return fidl::InterfaceHandle<fuchsia::io::File>(zx::channel(handle));
}

static fidl::InterfaceHandle<fuchsia::io::File> GetExtrasPartition() {
  TRACE_DURATION("linux_runner", "GetExtrasPartition");
  int fd = open(kExtrasImagePath, O_RDONLY);
  if (fd < 0) {
    return nullptr;
  }
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_get_service_handle(fd, &handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get service handle: " << status;
    return nullptr;
  }
  return fidl::InterfaceHandle<fuchsia::io::File>(zx::channel(handle));
}

static fidl::VectorPtr<fuchsia::virtualization::BlockDevice> GetBlockDevices(
    size_t stateful_image_size) {
  TRACE_DURATION("linux_runner", "GetBlockDevices");
  auto file_handle = GetOrCreateStatefulPartition(stateful_image_size);
  FXL_CHECK(file_handle) << "Failed to open stateful file";
  std::vector<fuchsia::virtualization::BlockDevice> devices;
#ifdef USE_VOLATILE_BLOCK
  auto stateful_block_mode = fuchsia::virtualization::BlockMode::VOLATILE_WRITE;
#else
  auto stateful_block_mode = fuchsia::virtualization::BlockMode::READ_WRITE;
#endif
  devices.push_back({
      "stateful",
      stateful_block_mode,
      fuchsia::virtualization::BlockFormat::RAW,
      std::move(file_handle),
  });
  auto extras_handle = GetExtrasPartition();
  if (extras_handle) {
    devices.push_back({
        "extras",
        fuchsia::virtualization::BlockMode::VOLATILE_WRITE,
        fuchsia::virtualization::BlockFormat::RAW,
        std::move(extras_handle),
    });
  }
  return devices;
}

// static
zx_status_t Guest::CreateAndStart(sys::ComponentContext* context, GuestConfig config,
                                  std::unique_ptr<Guest>* guest) {
  TRACE_DURATION("linux_runner", "Guest::CreateAndStart");
  fuchsia::virtualization::ManagerPtr guestmgr;
  context->svc()->Connect(guestmgr.NewRequest());
  fuchsia::virtualization::RealmPtr guest_env;
  guestmgr->Create(kLinuxEnvirionmentName, guest_env.NewRequest());

  *guest = std::make_unique<Guest>(context, config, std::move(guest_env));
  return ZX_OK;
}

Guest::Guest(sys::ComponentContext* context, GuestConfig config,
             fuchsia::virtualization::RealmPtr env)
    : async_(async_get_default_dispatcher()),
      executor_(async_),
      config_(config),
      guest_env_(std::move(env)),
      wayland_dispatcher_(context, fit::bind_member(this, &Guest::OnNewView)) {
  guest_env_->GetHostVsockEndpoint(socket_endpoint_.NewRequest());
  executor_.schedule_task(Start());
}

Guest::~Guest() {
  if (grpc_server_) {
    grpc_server_->inner()->Shutdown();
    grpc_server_->inner()->Wait();
  }
}

fit::promise<> Guest::Start() {
  TRACE_DURATION("linux_runner", "Guest::Start");
  return StartGrpcServer()
      .and_then([this](std::unique_ptr<GrpcVsockServer>& server) mutable
                -> fit::result<void, zx_status_t> {
        grpc_server_ = std::move(server);
        StartGuest();
        return fit::ok();
      })
      .or_else([](const zx_status_t& status) {
        FXL_LOG(ERROR) << "Failed to start guest: " << status;
        return fit::ok();
      });
}

fit::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t> Guest::StartGrpcServer() {
  TRACE_DURATION("linux_runner", "Guest::StartGrpcServer");
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint;
  guest_env_->GetHostVsockEndpoint(socket_endpoint.NewRequest());
  GrpcVsockServerBuilder builder(std::move(socket_endpoint));

  // LogCollector
  builder.AddListenPort(kLogCollectorPort);
  builder.RegisterService(&log_collector_);

  // StartupListener
  builder.AddListenPort(kStartupListenerPort);
  builder.RegisterService(static_cast<vm_tools::StartupListener::Service*>(this));

  // TremplinListener
  builder.AddListenPort(kTremplinListenerPort);
  builder.RegisterService(static_cast<vm_tools::tremplin::TremplinListener::Service*>(this));

  // ContainerListener
  builder.AddListenPort(kGarconPort);
  builder.RegisterService(static_cast<vm_tools::container::ContainerListener::Service*>(this));
  return builder.Build();
}

void Guest::StartGuest() {
  TRACE_DURATION("linux_runner", "Guest::StartGuest");
  FXL_CHECK(!guest_controller_) << "Called StartGuest with an existing instance";
  FXL_LOG(INFO) << "Launching guest...";

  fuchsia::virtualization::LaunchInfo launch_info;
  launch_info.url = kLinuxGuestPackage;
  launch_info.guest_config.set_virtio_gpu(false);
  launch_info.block_devices = GetBlockDevices(config_.stateful_image_size);
  launch_info.wayland_device = fuchsia::virtualization::WaylandDevice::New();
  launch_info.wayland_device->dispatcher = wayland_dispatcher_.NewBinding();
  launch_info.magma_device = fuchsia::virtualization::MagmaDevice::New();

  auto vm_create_nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("linux_runner", "LaunchInstance", vm_create_nonce);
  guest_env_->LaunchInstance(std::move(launch_info), guest_controller_.NewRequest(),
                             [this, vm_create_nonce](uint32_t cid) {
                               TRACE_DURATION("linux_runner", "LaunchInstance Callback");
                               TRACE_FLOW_END("linux_runner", "LaunchInstance", vm_create_nonce);
                               FXL_LOG(INFO) << "Guest launched with CID " << cid;
                               guest_cid_ = cid;
                               TRACE_FLOW_BEGIN("linux_runner", "TerminaBoot", vm_ready_nonce_);
                             });
}

void Guest::MountExtrasPartition() {
  TRACE_DURATION("linux_runner", "Guest::MountExtrasPartition");
  FXL_CHECK(maitred_) << "Called MountExtrasPartition without a maitre'd connection";
  FXL_LOG(INFO) << "Mounting Extras Partition";

  grpc::ClientContext context;
  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->assign("/dev/vdc");
  request.mutable_target()->assign("/mnt/shared");
  request.mutable_fstype()->assign("romfs");
  request.mutable_options()->assign("");
  request.set_mountflags(0);

  {
    TRACE_DURATION("linux_runner", "MountRPC");
    auto grpc_status = maitred_->Mount(&context, request, &response);
    FXL_CHECK(grpc_status.ok()) << "Failed to mount extras filesystem: "
                                << grpc_status.error_message();
  }
  FXL_LOG(INFO) << "Mounted Filesystem: " << response.error();
}

void Guest::ConfigureNetwork() {
  TRACE_DURATION("linux_runner", "Guest::ConfigureNetwork");
  FXL_CHECK(maitred_) << "Called ConfigureNetwork without a maitre'd connection";
  struct in_addr addr;

  uint32_t ip_addr = 0;
  FXL_LOG(INFO) << "Using ip: " << LINUX_RUNNER_IP_DEFAULT;
  FXL_CHECK(inet_aton(LINUX_RUNNER_IP_DEFAULT, &addr) != 0) << "Failed to parse address string";
  ip_addr = addr.s_addr;

  uint32_t netmask = 0;
  FXL_LOG(INFO) << "Using netmask: " << LINUX_RUNNER_NETMASK_DEFAULT;
  FXL_CHECK(inet_aton(LINUX_RUNNER_NETMASK_DEFAULT, &addr) != 0)
      << "Failed to parse address string";
  netmask = addr.s_addr;

  uint32_t gateway = 0;
  FXL_LOG(INFO) << "Using gateway: " << LINUX_RUNNER_GATEWAY_DEFAULT;
  FXL_CHECK(inet_aton(LINUX_RUNNER_GATEWAY_DEFAULT, &addr) != 0)
      << "Failed to parse address string";
  gateway = addr.s_addr;
  FXL_LOG(INFO) << "Configuring Guest Network...";

  grpc::ClientContext context;
  vm_tools::NetworkConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::IPv4Config* config = request.mutable_ipv4_config();
  config->set_address(ip_addr);
  config->set_gateway(gateway);
  config->set_netmask(netmask);

  {
    TRACE_DURATION("linux_runner", "ConfigureNetworkRPC");
    auto grpc_status = maitred_->ConfigureNetwork(&context, request, &response);
    FXL_CHECK(grpc_status.ok()) << "Failed to configure guest network: "
                                << grpc_status.error_message();
  }
  FXL_LOG(INFO) << "Network configured.";
}

void Guest::StartTermina() {
  TRACE_DURATION("linux_runner", "Guest::StartTermina");
  FXL_CHECK(maitred_) << "Called StartTermina without a maitre'd connection";
  FXL_LOG(INFO) << "Starting Termina...";

  grpc::ClientContext context;
  vm_tools::StartTerminaRequest request;
  vm_tools::StartTerminaResponse response;
  std::string lxd_subnet = "100.115.92.1/24";
  request.mutable_lxd_ipv4_subnet()->swap(lxd_subnet);

  {
    TRACE_DURATION("linux_runner", "StartTerminaRPC");
    auto grpc_status = maitred_->StartTermina(&context, request, &response);
    FXL_CHECK(grpc_status.ok()) << "Failed to start Termina: " << grpc_status.error_message();
  }
}

// This exposes a shell on /dev/hvc0 that can be used to interact with the
// VM.
void Guest::LaunchContainerShell() {
  FXL_CHECK(maitred_) << "Called LaunchShell without a maitre'd connection";
  FXL_LOG(INFO) << "Launching container shell...";

  grpc::ClientContext context;
  vm_tools::LaunchProcessRequest request;
  vm_tools::LaunchProcessResponse response;

  request.add_argv()->assign("/usr/bin/lxc");
  request.add_argv()->assign("exec");
  request.add_argv()->assign(kContainerName);
  request.add_argv()->assign("--");
  request.add_argv()->assign("/bin/login");
  request.add_argv()->assign("-f");
  request.add_argv()->assign(kDefaultContainerUser);

  request.set_respawn(true);
  request.set_use_console(true);
  request.set_wait_for_exit(false);
  {
    auto env = request.mutable_env();
    env->insert({"LXD_DIR", "/mnt/stateful/lxd"});
    env->insert({"LXD_CONF", "/mnt/stateful/lxd_conf"});
    env->insert({"LXD_UNPRIVILEGED_ONLY", "true"});
  }

  {
    TRACE_DURATION("linux_runner", "LaunchProcessRPC");
    auto status = maitred_->LaunchProcess(&context, request, &response);
    FXL_CHECK(status.ok()) << "Failed to launch container shell: " << status.error_message();
  }
}

void Guest::CreateContainer() {
  TRACE_DURATION("linux_runner", "Guest::CreateContainer");
  FXL_CHECK(tremplin_) << "CreateContainer called without a Tremplin connection";
  FXL_LOG(INFO) << "Creating Container...";

  grpc::ClientContext context;
  vm_tools::tremplin::CreateContainerRequest request;
  vm_tools::tremplin::CreateContainerResponse response;

  request.mutable_container_name()->assign(kContainerName);
  request.mutable_image_alias()->assign(kContainerImageAlias);
  request.mutable_image_server()->assign(kContainerImageServer);

  {
    TRACE_DURATION("linux_runner", "CreateContainerRPC");
    auto status = tremplin_->CreateContainer(&context, request, &response);
    FXL_CHECK(status.ok()) << "Failed to create container: " << status.error_message();
  }
  switch (response.status()) {
    case vm_tools::tremplin::CreateContainerResponse::CREATING:
      break;
    case vm_tools::tremplin::CreateContainerResponse::EXISTS:
      FXL_LOG(INFO) << "Container already exists";
      StartContainer();
      break;
    case vm_tools::tremplin::CreateContainerResponse::FAILED:
      FXL_LOG(ERROR) << "Failed to create container: " << response.failure_reason();
      break;
    case vm_tools::tremplin::CreateContainerResponse::UNKNOWN:
    default:
      FXL_LOG(ERROR) << "Unknown status: " << response.status();
      break;
  }
}

void Guest::StartContainer() {
  TRACE_DURATION("linux_runner", "Guest::StartContainer");
  FXL_CHECK(tremplin_) << "StartContainer called without a Tremplin connection";
  FXL_LOG(INFO) << "Starting Container...";

  grpc::ClientContext context;
  vm_tools::tremplin::StartContainerRequest request;
  vm_tools::tremplin::StartContainerResponse response;

  request.mutable_container_name()->assign(kContainerName);
  request.mutable_host_public_key()->assign("");
  request.mutable_container_private_key()->assign("");
  request.mutable_token()->assign("container_token");

  {
    TRACE_DURATION("linux_runner", "StartContainerRPC");
    auto status = tremplin_->StartContainer(&context, request, &response);
    FXL_CHECK(status.ok()) << "Failed to start container: " << status.error_message();
  }

  switch (response.status()) {
    case vm_tools::tremplin::StartContainerResponse::RUNNING:
    case vm_tools::tremplin::StartContainerResponse::STARTED:
      FXL_LOG(INFO) << "Container started";
      SetupUser();
      break;
    case vm_tools::tremplin::StartContainerResponse::FAILED:
      FXL_LOG(ERROR) << "Failed to start container: " << response.failure_reason();
      break;
    case vm_tools::tremplin::StartContainerResponse::UNKNOWN:
    default:
      FXL_LOG(ERROR) << "Unknown status: " << response.status();
      break;
  }
}

void Guest::SetupUser() {
  FXL_CHECK(tremplin_) << "SetupUser called without a Tremplin connection";
  FXL_LOG(INFO) << "Creating user '" << kDefaultContainerUser << "'...";

  grpc::ClientContext context;
  vm_tools::tremplin::SetUpUserRequest request;
  vm_tools::tremplin::SetUpUserResponse response;

  request.mutable_container_name()->assign(kContainerName);
  request.mutable_container_username()->assign(kDefaultContainerUser);
  {
    TRACE_DURATION("linux_runner", "SetUpUserRPC");
    auto status = tremplin_->SetUpUser(&context, request, &response);
    FXL_CHECK(status.ok()) << "Failed to setup user '" << kDefaultContainerUser
                           << "': " << status.error_message();
  }

  switch (response.status()) {
    case vm_tools::tremplin::SetUpUserResponse::EXISTS:
    case vm_tools::tremplin::SetUpUserResponse::SUCCESS:
      FXL_LOG(INFO) << "User created.";
      LaunchContainerShell();
      break;
    case vm_tools::tremplin::SetUpUserResponse::FAILED:
      FXL_LOG(ERROR) << "Failed to create user: " << response.failure_reason();
      break;
    case vm_tools::tremplin::SetUpUserResponse::UNKNOWN:
    default:
      FXL_LOG(ERROR) << "Unknown status: " << response.status();
      break;
  }
}

grpc::Status Guest::VmReady(grpc::ServerContext* context, const vm_tools::EmptyMessage* request,
                            vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::VmReady");
  TRACE_FLOW_END("linux_runner", "TerminaBoot", vm_ready_nonce_);
  FXL_LOG(INFO) << "VM Ready -- Connecting to Maitre'd...";
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  auto p = NewGrpcVsockStub<vm_tools::Maitred>(socket_endpoint_, guest_cid_, kMaitredPort)
               .then([this](fit::result<std::unique_ptr<vm_tools::Maitred::Stub>, zx_status_t>&
                                result) mutable {
                 if (result.is_ok()) {
                   this->maitred_ = std::move(result.value());
                   MountExtrasPartition();
                   ConfigureNetwork();
                   StartTermina();
                 } else {
                   FXL_CHECK(false) << "Failed to connect to Maitre'd";
                 }
               });
  executor_.schedule_task(std::move(p));
  return grpc::Status::OK;
}

grpc::Status Guest::TremplinReady(grpc::ServerContext* context,
                                  const ::vm_tools::tremplin::TremplinStartupInfo* request,
                                  vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::TremplinReady");
  FXL_LOG(INFO) << "Tremplin Ready.";
  auto p =
      NewGrpcVsockStub<vm_tools::tremplin::Tremplin>(socket_endpoint_, guest_cid_, kTremplinPort)
          .then([this](fit::result<std::unique_ptr<vm_tools::tremplin::Tremplin::Stub>,
                                   zx_status_t>& result) mutable -> fit::result<> {
            if (result.is_ok()) {
              tremplin_ = std::move(result.value());
              CreateContainer();
            } else {
              FXL_LOG(ERROR) << "Failed to connect to tremplin";
            }
            return fit::ok();
          });
  executor_.schedule_task(std::move(p));
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateCreateStatus(grpc::ServerContext* context,
                                       const vm_tools::tremplin::ContainerCreationProgress* request,
                                       vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateCreateStatus");
  switch (request->status()) {
    case vm_tools::tremplin::ContainerCreationProgress::CREATED:
      FXL_LOG(INFO) << "Container created: " << request->container_name();
      StartContainer();
      break;
    case vm_tools::tremplin::ContainerCreationProgress::DOWNLOADING:
      FXL_LOG(INFO) << "Downloading " << request->container_name() << ": "
                    << request->download_progress() << "%";
      break;
    case vm_tools::tremplin::ContainerCreationProgress::DOWNLOAD_TIMED_OUT:
      FXL_LOG(INFO) << "Download timed out for " << request->container_name();
      break;
    case vm_tools::tremplin::ContainerCreationProgress::CANCELLED:
      FXL_LOG(INFO) << "Download cancelled for " << request->container_name();
      break;
    case vm_tools::tremplin::ContainerCreationProgress::FAILED:
      FXL_LOG(INFO) << "Download failed for " << request->container_name() << ": "
                    << request->failure_reason();
      break;
    case vm_tools::tremplin::ContainerCreationProgress::UNKNOWN:
    default:
      FXL_LOG(INFO) << "Unknown download status: " << request->status();
      break;
  }
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateDeletionStatus(
    ::grpc::ServerContext* context, const ::vm_tools::tremplin::ContainerDeletionProgress* request,
    ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateDeletionStatus");
  FXL_LOG(INFO) << "Update Deletion Status";
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateStartStatus(::grpc::ServerContext* context,
                                      const ::vm_tools::tremplin::ContainerStartProgress* request,
                                      ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateStartStatus");
  FXL_LOG(INFO) << "Update Start Status";
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateExportStatus(::grpc::ServerContext* context,
                                       const ::vm_tools::tremplin::ContainerExportProgress* request,
                                       ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateExportStatus");
  FXL_LOG(INFO) << "Update Export Status";
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateImportStatus(::grpc::ServerContext* context,
                                       const ::vm_tools::tremplin::ContainerImportProgress* request,
                                       ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateImportStatus");
  FXL_LOG(INFO) << "Update Import Status";
  return grpc::Status::OK;
}
grpc::Status Guest::ContainerShutdown(::grpc::ServerContext* context,
                                      const ::vm_tools::tremplin::ContainerShutdownInfo* request,
                                      ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::ContainerShutdown");
  FXL_LOG(INFO) << "Container Shutdown";
  return grpc::Status::OK;
}

grpc::Status Guest::ContainerReady(grpc::ServerContext* context,
                                   const vm_tools::container::ContainerStartupInfo* request,
                                   vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::ContainerReady");
  // TODO(tjdetwiler): validate token.
  auto garcon_port = request->garcon_port();
  FXL_LOG(INFO) << "Container Ready; Garcon listening on port " << garcon_port;
  auto p =
      NewGrpcVsockStub<vm_tools::container::Garcon>(socket_endpoint_, guest_cid_, garcon_port)
          .then([this](fit::result<std::unique_ptr<vm_tools::container::Garcon::Stub>, zx_status_t>&
                           result) mutable -> fit::result<> {
            if (result.is_ok()) {
              garcon_ = std::move(result.value());

              DumpContainerDebugInfo();

              for (auto it = pending_requests_.begin(); it != pending_requests_.end();
                   it = pending_requests_.erase(it)) {
                LaunchApplication(std::move(*it));
              }
            } else {
              FXL_LOG(ERROR) << "Failed to connect to garcon";
            }
            return fit::ok();
          });
  executor_.schedule_task(std::move(p));

  return grpc::Status::OK;
}

grpc::Status Guest::ContainerShutdown(grpc::ServerContext* context,
                                      const vm_tools::container::ContainerShutdownInfo* request,
                                      vm_tools::EmptyMessage* response) {
  FXL_LOG(INFO) << "Container Shutdown";
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateApplicationList(
    grpc::ServerContext* context, const vm_tools::container::UpdateApplicationListRequest* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateApplicationList");
  FXL_LOG(INFO) << "Update Application List";
  for (const auto& application : request->application()) {
    FXL_LOG(INFO) << "ID: " << application.desktop_file_id();
    const auto& name = application.name().values().begin();
    if (name != application.name().values().end()) {
      FXL_LOG(INFO) << "\tname:             " << name->value();
    }
    const auto& comment = application.comment().values().begin();
    if (comment != application.comment().values().end()) {
      FXL_LOG(INFO) << "\tcomment:          " << comment->value();
    }
    FXL_LOG(INFO) << "\tno_display:       " << application.no_display();
    FXL_LOG(INFO) << "\tstartup_wm_class: " << application.startup_wm_class();
    FXL_LOG(INFO) << "\tstartup_notify:   " << application.startup_notify();
    FXL_LOG(INFO) << "\tpackage_id:       " << application.package_id();
  }
  return grpc::Status::OK;
}

grpc::Status Guest::OpenUrl(grpc::ServerContext* context,
                            const vm_tools::container::OpenUrlRequest* request,
                            vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::OpenUrl");
  FXL_LOG(INFO) << "Open URL";
  return grpc::Status::OK;
}

grpc::Status Guest::InstallLinuxPackageProgress(
    grpc::ServerContext* context,
    const vm_tools::container::InstallLinuxPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::InstallLinuxPackageProgress");
  FXL_LOG(INFO) << "Install Linux Package Progress";
  return grpc::Status::OK;
}

grpc::Status Guest::UninstallPackageProgress(
    grpc::ServerContext* context, const vm_tools::container::UninstallPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UninstallPackageProgress");
  FXL_LOG(INFO) << "Uninstall Package Progress";
  return grpc::Status::OK;
}

grpc::Status Guest::OpenTerminal(grpc::ServerContext* context,
                                 const vm_tools::container::OpenTerminalRequest* request,
                                 vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::OpenTerminal");
  FXL_LOG(INFO) << "Open Terminal";
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateMimeTypes(grpc::ServerContext* context,
                                    const vm_tools::container::UpdateMimeTypesRequest* request,
                                    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateMimeTypes");
  FXL_LOG(INFO) << "Update Mime Types";
  size_t i = 0;
  for (const auto& pair : request->mime_type_mappings()) {
    FXL_LOG(INFO) << "\t" << pair.first << ": " << pair.second;
    if (++i > 10) {
      FXL_LOG(INFO) << "\t..." << (request->mime_type_mappings_size() - i) << " more.";
      break;
    }
  }
  return grpc::Status::OK;
}

void Guest::DumpContainerDebugInfo() {
  FXL_CHECK(garcon_) << "Called DumpContainerDebugInfo without a garcon connection";
  FXL_LOG(INFO) << "Dumping Container Debug Info...";

  grpc::ClientContext context;
  vm_tools::container::GetDebugInformationRequest request;
  vm_tools::container::GetDebugInformationResponse response;

  auto grpc_status = garcon_->GetDebugInformation(&context, request, &response);
  if (!grpc_status.ok()) {
    FXL_LOG(ERROR) << "Failed to read container debug information: " << grpc_status.error_message();
    return;
  }

  FXL_LOG(INFO) << "Container debug information:";
  FXL_LOG(INFO) << response.debug_information();
}

void Guest::Launch(AppLaunchRequest request) {
  TRACE_DURATION("linux_runner", "Guest::Launch");
  // If we have a garcon connection we can request the launch immediately.
  // Otherwise we just retain the request and forward it along once the
  // container is started.
  if (garcon_) {
    LaunchApplication(std::move(request));
    return;
  }
  pending_requests_.push_back(std::move(request));
}

void Guest::LaunchApplication(AppLaunchRequest app) {
  TRACE_DURATION("linux_runner", "Guest::LaunchApplication");
  FXL_CHECK(garcon_) << "Called LaunchApplication without a garcon connection";
  std::string desktop_file_id = app.application.resolved_url;
  if (desktop_file_id.rfind(kLinuxUriScheme, 0) == std::string::npos) {
    FXL_LOG(ERROR) << "Invalid URI: " << desktop_file_id;
    return;
  }
  desktop_file_id.erase(0, strlen(kLinuxUriScheme));
  if (desktop_file_id == "") {
    // HACK: we use the empty URI to pick up a view that wasn't associated
    // with an app launch request. For example, if you started a GUI
    // application from the serial console, a wayland view will have been
    // created without a fuchsia component to associate with it.
    //
    // We'll need to come up with a more proper solution, but this allows us to
    // at least do some testing of these views for the time being.
    auto it = background_views_.begin();
    if (it == background_views_.end()) {
      FXL_LOG(INFO) << "No background views available";
      return;
    }
    CreateComponent(std::move(app), it->Bind());
    background_views_.erase(it);
    return;
  }

  FXL_LOG(INFO) << "Launching: " << desktop_file_id;
  grpc::ClientContext context;
  vm_tools::container::LaunchApplicationRequest request;
  vm_tools::container::LaunchApplicationResponse response;

  request.set_desktop_file_id(std::move(desktop_file_id));
  {
    TRACE_DURATION("linux_runner", "LaunchApplicationRPC");
    auto grpc_status = garcon_->LaunchApplication(&context, request, &response);
    if (!grpc_status.ok() || !response.success()) {
      FXL_LOG(ERROR) << "Failed to launch application: " << grpc_status.error_message() << ", "
                     << response.failure_reason();
      return;
    }
  }

  FXL_LOG(INFO) << "Application launched successfully";
  pending_views_.push_back(std::move(app));
}

void Guest::OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view_provider) {
  TRACE_DURATION("linux_runner", "Guest::OnNewView");
  // TODO: This currently just pops a component request off the queue to
  // associate with the new view. This is obviously racy but will work until
  // we can pipe though a startup id to provide a more accurate correlation.
  auto it = pending_views_.begin();
  if (it == pending_views_.end()) {
    background_views_.push_back(std::move(view_provider));
    return;
  }
  CreateComponent(std::move(*it), std::move(view_provider));
  pending_views_.erase(it);
}

void Guest::CreateComponent(AppLaunchRequest request,
                            fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view_provider) {
  TRACE_DURATION("linux_runner", "Guest::CreateComponent");
  auto component = LinuxComponent::Create(
      fit::bind_member(this, &Guest::OnComponentTerminated), std::move(request.application),
      std::move(request.startup_info), std::move(request.controller_request), view_provider.Bind());
  components_.insert({component.get(), std::move(component)});
}

void Guest::OnComponentTerminated(const LinuxComponent* component) { components_.erase(component); }

}  // namespace linux_runner
