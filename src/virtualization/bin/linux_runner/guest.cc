// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/guest.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/device/cpp/fidl.h>
#include <fuchsia/hardware/block/volume/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <netinet/in.h>
#include <sys/mount.h>
#include <unistd.h>
#include <zircon/hw/gpt.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/virtualization/bin/linux_runner/ports.h"
#include "src/virtualization/lib/grpc/grpc_vsock_stub.h"
#include "src/virtualization/lib/guest_config/guest_config.h"
#include "src/virtualization/third_party/vm_tools/vm_guest.grpc.pb.h"

#include <grpc++/grpc++.h>
#include <grpc++/server_posix.h>

namespace {

constexpr const char kLinuxGuestPackage[] =
    "fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cmx";
constexpr const char kContainerName[] = "penguin";
constexpr const char kContainerImageAlias[] = "debian/bullseye";
constexpr const char kContainerImageServer[] = "https://storage.googleapis.com/cros-containers/96";
constexpr const char kDefaultContainerUser[] = "machina";
constexpr const char kWaylandBridgePackage[] =
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx";

#if defined(USE_VOLATILE_BLOCK)
constexpr bool kForceVolatileWrites = true;
#else
constexpr bool kForceVolatileWrites = false;
#endif

constexpr size_t kNumRetries = 5;
constexpr auto kRetryDelay = zx::msec(100);
constexpr const char kBlockPath[] = "/dev/class/block";
constexpr auto kGuidSize = fuchsia::hardware::block::partition::GUID_LENGTH;
constexpr const char kGuestPartitionName[] = "guest";
constexpr std::array<uint8_t, kGuidSize> kGuestPartitionGuid = {
    0x9a, 0x17, 0x7d, 0x2d, 0x8b, 0x24, 0x4a, 0x4c, 0x87, 0x11, 0x1f, 0x99, 0x05, 0xb7, 0x6e, 0xd1,
};
constexpr std::array<uint8_t, kGuidSize> kFvmGuid = GUID_FVM_VALUE;
constexpr std::array<uint8_t, kGuidSize> kGptFvmGuid = GPT_FVM_TYPE_GUID;

using VolumeHandle = fidl::InterfaceHandle<fuchsia::hardware::block::volume::Volume>;
using ManagerHandle = fidl::InterfaceHandle<fuchsia::hardware::block::volume::VolumeManager>;

// Information about a disk image.
struct DiskImage {
  const char* path;                             // Path to the file containing the image
  fuchsia::virtualization::BlockFormat format;  // Format of the disk image
  bool read_only;
};

#ifdef USE_PREBUILT_STATEFUL_IMAGE
constexpr DiskImage kStatefulImage = DiskImage{
    .path = "/pkg/data/stateful.qcow2",
    .format = fuchsia::virtualization::BlockFormat::QCOW,
    .read_only = true,
};
#else
constexpr DiskImage kStatefulImage = DiskImage{
    .format = fuchsia::virtualization::BlockFormat::BLOCK,
    .read_only = false,
};
#endif

constexpr DiskImage kExtrasImage = DiskImage{
    .path = "/pkg/data/extras.img",
    .format = fuchsia::virtualization::BlockFormat::FILE,
    .read_only = true,
};

// Finds the guest FVM partition, and the FVM GPT partition.
zx::status<std::tuple<VolumeHandle, ManagerHandle>> FindPartitions(DIR* dir) {
  VolumeHandle volume;
  ManagerHandle manager;

  fdio_cpp::UnownedFdioCaller caller(dirfd(dir));
  for (dirent* entry; (entry = readdir(dir)) != nullptr;) {
    fuchsia::hardware::block::partition::PartitionSyncPtr partition;
    zx_status_t status = fdio_service_connect_at(caller.borrow_channel(), entry->d_name,
                                                 partition.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to connect to '" << entry->d_name
                     << "': " << zx_status_get_string(status);
      return zx::error(status);
    }

    zx_status_t guid_status;
    std::unique_ptr<fuchsia::hardware::block::partition::GUID> guid;
    status = partition->GetTypeGuid(&guid_status, &guid);
    if (status != ZX_OK || guid_status != ZX_OK || !guid) {
      continue;
    } else if (std::equal(kGuestPartitionGuid.begin(), kGuestPartitionGuid.end(),
                          guid->value.begin())) {
      // If we find the guest FVM partition, then we can break out of the loop.
      // We only need to find the FVM GPT partition if there is no guest FVM
      // partition, in order to create the guest FVM partition.
      volume.set_channel(partition.Unbind().TakeChannel());
      break;
    } else if (std::equal(kFvmGuid.begin(), kFvmGuid.end(), guid->value.begin()) ||
               std::equal(kGptFvmGuid.begin(), kGptFvmGuid.end(), guid->value.begin())) {
      fuchsia::device::ControllerSyncPtr controller;
      controller.Bind(partition.Unbind().TakeChannel());
      fuchsia::device::Controller_GetTopologicalPath_Result topo_result;
      status = controller->GetTopologicalPath(&topo_result);
      if (status != ZX_OK || topo_result.is_err()) {
        FX_LOGS(ERROR) << "Failed to get topological path for '" << entry->d_name << "'";
        return zx::error(ZX_ERR_IO);
      }

      auto fvm_path = topo_result.response().path + "/fvm";
      status = fdio_service_connect(fvm_path.data(), manager.NewRequest().TakeChannel().release());
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to connect to '" << fvm_path
                       << "': " << zx_status_get_string(status);
        return zx::error(status);
      }
    }
  }

  return zx::ok(std::make_tuple(std::move(volume), std::move(manager)));
}

// Waits for the guest partition to be allocated.
//
// TODO(fxbug.dev/90469): Use a directory watcher instead of scanning for
// new partitions.
zx::status<VolumeHandle> WaitForPartition(DIR* dir) {
  for (size_t retry = 0; retry != kNumRetries; retry++) {
    auto partitions = FindPartitions(dir);
    if (partitions.is_error()) {
      return partitions.take_error();
    }
    auto& [volume, manager] = *partitions;
    if (volume) {
      return zx::ok(std::move(volume));
    }
    zx::nanosleep(zx::deadline_after(kRetryDelay));
  }
  FX_LOGS(ERROR) << "Failed to create guest partition";
  return zx::error(ZX_ERR_IO);
}

// Locates the FVM partition for a guest block device. If a partition does not
// exist, allocate one.
zx::status<VolumeHandle> FindOrAllocatePartition(std::string_view path, size_t partition_size) {
  auto dir = opendir(path.data());
  if (dir == nullptr) {
    FX_LOGS(ERROR) << "Failed to open directory '" << path << "'";
    return zx::error(ZX_ERR_IO);
  }
  auto defer = fit::defer([dir] { closedir(dir); });

  auto partitions = FindPartitions(dir);
  if (partitions.is_error()) {
    return partitions.take_error();
  }
  auto& [volume, manager] = *partitions;

  if (!volume) {
    if (!manager) {
      FX_LOGS(ERROR) << "Failed to find FVM";
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    auto sync = manager.BindSync();
    zx_status_t info_status = ZX_OK;
    // Get the partition slice size.
    std::unique_ptr<fuchsia::hardware::block::volume::VolumeManagerInfo> info;
    zx_status_t status = sync->GetInfo(&info_status, &info);
    if (status != ZX_OK || info_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get volume info: " << zx_status_get_string(status) << " and "
                     << zx_status_get_string(info_status);
      return zx::error(ZX_ERR_IO);
    }
    size_t slices = partition_size / info->slice_size;
    zx_status_t part_status = ZX_OK;
    status = sync->AllocatePartition(slices, {.value = kGuestPartitionGuid}, {},
                                     kGuestPartitionName, 0, &part_status);
    if (status != ZX_OK || part_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to allocate partition: " << zx_status_get_string(status) << " and "
                     << zx_status_get_string(part_status);
      return zx::error(ZX_ERR_IO);
    }
    return WaitForPartition(dir);
  }

  return zx::ok(std::move(volume));
}

// Opens the given disk image.
zx::status<fuchsia::io::FileHandle> GetPartition(const DiskImage& image) {
  TRACE_DURATION("linux_runner", "GetPartition");
  uint32_t flags = fuchsia::io::OPEN_RIGHT_READABLE;
  if (!image.read_only) {
    flags |= fuchsia::io::OPEN_RIGHT_WRITABLE;
  }
  fuchsia::io::FileHandle file;
  zx_status_t status = fdio_open(image.path, flags, file.NewRequest().TakeChannel().release());
  if (status) {
    return zx::error(status);
  }
  return zx::ok(std::move(file));
}

// Return the given IPv4 address as a packed uint32_t in network byte
// order (i.e., big endian).
//
// `Ipv4Addr(127, 0, 0, 1)` will generate the loopback address "127.0.0.1".
constexpr uint32_t Ipv4Addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return htonl((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(c) << 8) | (static_cast<uint32_t>(d) << 0));
}

// Run the given command in the guest as a daemon (i.e., in the background and
// automatically restarted on failure).
void MaitredStartDaemon(vm_tools::Maitred::Stub& maitred, std::vector<std::string> args,
                        std::vector<std::pair<std::string, std::string>> env) {
  grpc::ClientContext context;
  vm_tools::LaunchProcessRequest request;
  vm_tools::LaunchProcessResponse response;

  // Set up args / environment.
  request.mutable_argv()->Assign(args.begin(), args.end());
  request.mutable_env()->insert(env.begin(), env.end());

  // Set up as a daemon.
  request.set_use_console(true);
  request.set_respawn(true);
  request.set_wait_for_exit(false);

  TRACE_DURATION("linux_runner", "LaunchProcessRPC");
  grpc::Status status = maitred.LaunchProcess(&context, request, &response);
  FX_CHECK(status.ok()) << "Failed to start daemon in guest: " << status.error_message() << "\n"
                        << "Command run: " << request.DebugString();
  FX_CHECK(response.status() == vm_tools::ProcessStatus::LAUNCHED)
      << "Process failed to launch, with launch status: "
      << vm_tools::ProcessStatus_Name(response.status());
}

// Run the given command in the guest, blocking until finished.
void MaitredRunCommandSync(vm_tools::Maitred::Stub& maitred, std::vector<std::string> args,
                           std::vector<std::pair<std::string, std::string>> env) {
  grpc::ClientContext context;
  vm_tools::LaunchProcessRequest request;
  vm_tools::LaunchProcessResponse response;

  // Set up args / environment.
  request.mutable_argv()->Assign(args.begin(), args.end());
  request.mutable_env()->insert(env.begin(), env.end());

  // Set the command as synchronous.
  request.set_use_console(true);
  request.set_respawn(false);
  request.set_wait_for_exit(true);

  TRACE_DURATION("linux_runner", "LaunchProcessRPC");
  grpc::Status status = maitred.LaunchProcess(&context, request, &response);
  FX_CHECK(status.ok()) << "Guest command failed: " << status.error_message();
}

// Ask maitre'd to enable the network in the guest.
void MaitredBringUpNetwork(vm_tools::Maitred::Stub& maitred, uint32_t address, uint32_t gateway,
                           uint32_t netmask) {
  grpc::ClientContext context;
  vm_tools::NetworkConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::IPv4Config* config = request.mutable_ipv4_config();
  config->set_address(Ipv4Addr(100, 64, 1, 1));       // 100.64.1.1, RFC-6598 address
  config->set_gateway(Ipv4Addr(100, 64, 1, 2));       // 100.64.1.2, RFC-6598 address
  config->set_netmask(Ipv4Addr(255, 255, 255, 252));  // 30-bit netmask

  TRACE_DURATION("linux_runner", "ConfigureNetworkRPC");
  grpc::Status status = maitred.ConfigureNetwork(&context, request, &response);
  FX_CHECK(status.ok()) << "Failed to configure guest network: " << status.error_message();
}

}  // namespace

namespace linux_runner {

// static
zx_status_t Guest::CreateAndStart(sys::ComponentContext* context, GuestConfig config,
                                  GuestInfoCallback callback, std::unique_ptr<Guest>* guest) {
  TRACE_DURATION("linux_runner", "Guest::CreateAndStart");
  fuchsia::virtualization::ManagerPtr guestmgr;
  context->svc()->Connect(guestmgr.NewRequest());
  fuchsia::virtualization::RealmPtr guest_env;
  guestmgr->Create(config.env_label, guest_env.NewRequest());

  *guest = std::make_unique<Guest>(context, config, std::move(callback), std::move(guest_env));
  return ZX_OK;
}

Guest::Guest(sys::ComponentContext* context, GuestConfig config, GuestInfoCallback callback,
             fuchsia::virtualization::RealmPtr env)
    : async_(async_get_default_dispatcher()),
      executor_(async_),
      config_(config),
      callback_(std::move(callback)),
      guest_env_(std::move(env)),
      wayland_dispatcher_(context, kWaylandBridgePackage) {
  guest_env_->GetHostVsockEndpoint(socket_endpoint_.NewRequest());
  executor_.schedule_task(Start());
}

Guest::~Guest() {
  if (grpc_server_) {
    grpc_server_->inner()->Shutdown();
    grpc_server_->inner()->Wait();
  }
}

fpromise::promise<> Guest::Start() {
  TRACE_DURATION("linux_runner", "Guest::Start");
  return StartGrpcServer()
      .and_then([this](std::unique_ptr<GrpcVsockServer>& server) mutable
                -> fpromise::result<void, zx_status_t> {
        grpc_server_ = std::move(server);
        StartGuest();
        return fpromise::ok();
      })
      .or_else([](const zx_status_t& status) {
        FX_LOGS(ERROR) << "Failed to start guest: " << status;
        return fpromise::ok();
      });
}

fpromise::promise<std::unique_ptr<GrpcVsockServer>, zx_status_t> Guest::StartGrpcServer() {
  TRACE_DURATION("linux_runner", "Guest::StartGrpcServer");
  fuchsia::virtualization::HostVsockEndpointPtr socket_endpoint;
  guest_env_->GetHostVsockEndpoint(socket_endpoint.NewRequest());
  GrpcVsockServerBuilder builder(std::move(socket_endpoint));

  // CrashListener
  builder.AddListenPort(kCrashListenerPort);
  builder.RegisterService(&crash_listener_);

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

std::vector<fuchsia::virtualization::BlockSpec> Guest::GetBlockDevices(size_t stateful_image_size) {
  TRACE_DURATION("linux_runner", "Guest::GetBlockDevices");

  std::vector<fuchsia::virtualization::BlockSpec> devices;

  // Get/create the stateful partition.
  zx::channel stateful;
  if (kStatefulImage.format == fuchsia::virtualization::BlockFormat::BLOCK) {
    auto handle = FindOrAllocatePartition(kBlockPath, stateful_image_size);
    if (handle.is_error()) {
      PostContainerFailure("Failed to find or allocate a partition");
      return devices;
    }
    stateful = handle->TakeChannel();
  } else {
    auto handle = GetPartition(kStatefulImage);
    if (handle.is_error()) {
      PostContainerFailure("Failed to open or create stateful file");
      return devices;
    }
    stateful = handle->TakeChannel();
  }
  devices.push_back({
      .id = "stateful",
      .mode = (kStatefulImage.read_only || kForceVolatileWrites)
                  ? fuchsia::virtualization::BlockMode::VOLATILE_WRITE
                  : fuchsia::virtualization::BlockMode::READ_WRITE,
      .format = kStatefulImage.format,
      .client = std::move(stateful),
  });

  // Drop access to /dev, in order to prevent any further access.
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  FX_CHECK(status == ZX_OK) << "Failed to get installed namespace";
  if (fdio_ns_is_bound(ns, "/dev")) {
    status = fdio_ns_unbind(ns, "/dev");
    FX_CHECK(status == ZX_OK) << "Failed to unbind '/dev' from the installed namespace";
  }

  // Add the extras partition if it exists.
  auto extras = GetPartition(kExtrasImage);
  if (extras.is_ok()) {
    devices.push_back({
        .id = "extras",
        .mode = fuchsia::virtualization::BlockMode::VOLATILE_WRITE,
        .format = kExtrasImage.format,
        .client = extras->TakeChannel(),
    });
  }

  return devices;
}

void Guest::StartGuest() {
  TRACE_DURATION("linux_runner", "Guest::StartGuest");
  FX_CHECK(!guest_controller_) << "Called StartGuest with an existing instance";
  FX_LOGS(INFO) << "Launching guest...";

  auto block_devices = GetBlockDevices(config_.stateful_image_size);
  if (block_devices.empty()) {
    FX_LOGS(ERROR) << "Failed to start guest: missing block device";
    return;
  }

  fuchsia::virtualization::GuestConfig cfg;
  cfg.set_virtio_gpu(false);
  cfg.set_block_devices(std::move(block_devices));
  cfg.mutable_wayland_device()->server = wayland_dispatcher_.NewBinding();
  cfg.set_magma_device(fuchsia::virtualization::MagmaDevice());

  auto vm_create_nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("linux_runner", "LaunchInstance", vm_create_nonce);
  guest_env_->LaunchInstance(
      kLinuxGuestPackage, cpp17::nullopt, std::move(cfg), guest_controller_.NewRequest(),
      [this, vm_create_nonce](uint32_t cid) {
        TRACE_DURATION("linux_runner", "LaunchInstance Callback");
        TRACE_FLOW_END("linux_runner", "LaunchInstance", vm_create_nonce);
        FX_LOGS(INFO) << "Guest launched with CID " << cid;
        guest_cid_ = cid;
        PostContainerStatus(fuchsia::virtualization::ContainerStatus::LAUNCHING_GUEST);
        TRACE_FLOW_BEGIN("linux_runner", "TerminaBoot", vm_ready_nonce_);
      });
}

void Guest::MountVmTools() {
  TRACE_DURATION("linux_runner", "Guest::MountVmTools");
  FX_CHECK(maitred_) << "Called MountVmTools without a maitre'd connection";
  FX_LOGS(INFO) << "Mounting vm_tools";

  grpc::ClientContext context;
  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->assign("/dev/vdb");
  request.mutable_target()->assign("/opt/google/cros-containers");
  request.mutable_fstype()->assign("ext4");
  request.mutable_options()->assign("");
  request.set_mountflags(MS_RDONLY);

  {
    TRACE_DURATION("linux_runner", "MountRPC");
    auto grpc_status = maitred_->Mount(&context, request, &response);
    FX_CHECK(grpc_status.ok()) << "Failed to mount vm_tools partition: "
                               << grpc_status.error_message();
  }
  FX_LOGS(INFO) << "Mounted Filesystem: " << response.error();
}

void Guest::MountExtrasPartition() {
  TRACE_DURATION("linux_runner", "Guest::MountExtrasPartition");
  FX_CHECK(maitred_) << "Called MountExtrasPartition without a maitre'd connection";
  FX_LOGS(INFO) << "Mounting Extras Partition";

  grpc::ClientContext context;
  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->assign("/dev/vdd");
  request.mutable_target()->assign("/mnt/shared");
  request.mutable_fstype()->assign("romfs");
  request.mutable_options()->assign("");
  request.set_mountflags(0);

  {
    TRACE_DURATION("linux_runner", "MountRPC");
    auto grpc_status = maitred_->Mount(&context, request, &response);
    FX_CHECK(grpc_status.ok()) << "Failed to mount extras filesystem: "
                               << grpc_status.error_message();
  }
  FX_LOGS(INFO) << "Mounted Filesystem: " << response.error();
}

void Guest::ConfigureNetwork() {
  TRACE_DURATION("linux_runner", "Guest::ConfigureNetwork");
  FX_CHECK(maitred_) << "Called ConfigureNetwork without a maitre'd connection";

  FX_LOGS(INFO) << "Configuring Guest Network...";

  // Perform basic network bring up.
  //
  // To bring up the network, maitre'd requires an IPv4 address to use for the
  // guest's external NIC (even though we are going to replace it with
  // a DHCP-acquired address in just a moment).
  //
  // We use an RFC-6598 (carrier-grade NAT) IP address distinct from the LXD
  // subnet, but expect it to be overridden by DHCP later.
  MaitredBringUpNetwork(*maitred_,
                        /*address=*/Ipv4Addr(100, 64, 1, 1),      // 100.64.1.1, RFC-6598 address
                        /*gateway=*/Ipv4Addr(100, 64, 1, 2),      // 100.64.1.2, RFC-6598 address
                        /*netmask=*/Ipv4Addr(255, 255, 255, 252)  // 30-bit netmask
  );

  // Remove the configured IPv4 address from eth0.
  MaitredRunCommandSync(*maitred_, /*args=*/{"/bin/ip", "address", "flush", "eth0"}, /*env=*/{});

  // Run dhclient.
  MaitredStartDaemon(*maitred_,
                     /*args=*/
                     {
                         "/sbin/dhclient",
                         // Lease file
                         "-lf",
                         "/run/dhclient.leases",
                         // PID file
                         "-pf",
                         "/run/dhclient.pid",
                         // Do not detach, but remain in foreground so maitre'd can monitor.
                         "-d",
                         // Interface
                         "eth0",
                     },
                     /*env=*/{{"HOME", "/tmp"}, {"PATH", "/sbin:/bin"}});

  FX_LOGS(INFO) << "Network configured.";
}

void Guest::StartTermina() {
  TRACE_DURATION("linux_runner", "Guest::StartTermina");
  FX_CHECK(maitred_) << "Called StartTermina without a maitre'd connection";
  FX_LOGS(INFO) << "Starting Termina...";

  PostContainerStatus(fuchsia::virtualization::ContainerStatus::STARTING_VM);

  grpc::ClientContext context;
  vm_tools::StartTerminaRequest request;
  vm_tools::StartTerminaResponse response;
  std::string lxd_subnet = "100.115.92.1/24";
  request.mutable_lxd_ipv4_subnet()->swap(lxd_subnet);
  request.set_stateful_device("/dev/vdc");

  {
    TRACE_DURATION("linux_runner", "StartTerminaRPC");
    auto grpc_status = maitred_->StartTermina(&context, request, &response);
    FX_CHECK(grpc_status.ok()) << "Failed to start Termina: " << grpc_status.error_message();
  }
}

// This exposes a shell on /dev/hvc0 that can be used to interact with the
// VM.
void Guest::LaunchContainerShell() {
  FX_CHECK(maitred_) << "Called LaunchShell without a maitre'd connection";
  FX_LOGS(INFO) << "Launching container shell...";
  MaitredStartDaemon(
      *maitred_,
      {"/usr/bin/lxc", "exec", kContainerName, "--", "/bin/login", "-f", kDefaultContainerUser},
      {
          {"LXD_DIR", "/mnt/stateful/lxd"},
          {"LXD_CONF", "/mnt/stateful/lxd_conf"},
          {"LXD_UNPRIVILEGED_ONLY", "true"},
      });
}

void Guest::AddMagmaDeviceToContainer() {
  FX_CHECK(maitred_) << "Called AddMagma without a maitre'd connection";
  FX_LOGS(INFO) << "Adding magma device to container";
  MaitredRunCommandSync(*maitred_,
                        {"/usr/bin/lxc", "config", "device", "add", kContainerName, "magma0",
                         "unix-char", "source=/dev/magma0", "mode=0666"},
                        {
                            {"LXD_DIR", "/mnt/stateful/lxd"},
                            {"LXD_CONF", "/mnt/stateful/lxd_conf"},
                            {"LXD_UNPRIVILEGED_ONLY", "true"},
                        });
}

void Guest::SetupGPUDriversInContainer() {
  FX_CHECK(maitred_) << "Called SetupGPUDrivers without a maitre'd connection";
  FX_LOGS(INFO) << "Setup GPU drivers in container";
  MaitredRunCommandSync(
      *maitred_,
      {"/usr/bin/lxc", "exec", kContainerName, "--", "sh", "-c",
       "mkdir -p /usr/share/vulkan/icd.d; /usr/bin/update-alternatives --install "
       "/usr/share/vulkan/icd.d/10_magma_intel_icd.x86_64.json vulkan-icd "
       "/opt/google/cros-containers/share/vulkan/icd.d/intel_icd.x86_64.json 20; "
       "/usr/bin/update-alternatives --install "
       "/usr/share/vulkan/icd.d/10_magma_intel_icd.i686.json vulkan-icd32 "
       "/opt/google/cros-containers/share/vulkan/icd.d/intel_icd.i686.json 20; "
       "echo /opt/google/cros-containers/drivers/lib64=libc6 > /etc/ld.so.conf.d/cros.conf;"
       "echo /opt/google/cros-containers/drivers/lib32=libc6 >> /etc/ld.so.conf.d/cros.conf;"
       "/sbin/ldconfig; "},
      {
          {"LXD_DIR", "/mnt/stateful/lxd"},
          {"LXD_CONF", "/mnt/stateful/lxd_conf"},
          {"LXD_UNPRIVILEGED_ONLY", "true"},
      });
}

void Guest::CreateContainer() {
  TRACE_DURATION("linux_runner", "Guest::CreateContainer");
  FX_CHECK(tremplin_) << "CreateContainer called without a Tremplin connection";
  FX_LOGS(INFO) << "Creating Container...";

  grpc::ClientContext context;
  vm_tools::tremplin::CreateContainerRequest request;
  vm_tools::tremplin::CreateContainerResponse response;

  request.mutable_container_name()->assign(kContainerName);
  request.mutable_image_alias()->assign(kContainerImageAlias);
  request.mutable_image_server()->assign(kContainerImageServer);

  {
    TRACE_DURATION("linux_runner", "CreateContainerRPC");
    auto status = tremplin_->CreateContainer(&context, request, &response);
    FX_CHECK(status.ok()) << "Failed to create container: " << status.error_message();
  }
  switch (response.status()) {
    case vm_tools::tremplin::CreateContainerResponse::CREATING:
      break;
    case vm_tools::tremplin::CreateContainerResponse::EXISTS:
      FX_LOGS(INFO) << "Container already exists";
      StartContainer();
      break;
    case vm_tools::tremplin::CreateContainerResponse::FAILED:
      PostContainerFailure("Failed to create container: " + response.failure_reason());
      break;
    case vm_tools::tremplin::CreateContainerResponse::UNKNOWN:
    default:
      PostContainerFailure("Unknown status: " + std::to_string(response.status()));
      break;
  }
}

void Guest::StartContainer() {
  TRACE_DURATION("linux_runner", "Guest::StartContainer");
  FX_CHECK(tremplin_) << "StartContainer called without a Tremplin connection";
  FX_LOGS(INFO) << "Starting Container...";

  PostContainerStatus(fuchsia::virtualization::ContainerStatus::STARTING);

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
    FX_CHECK(status.ok()) << "Failed to start container: " << status.error_message();
  }

  switch (response.status()) {
    case vm_tools::tremplin::StartContainerResponse::RUNNING:
    case vm_tools::tremplin::StartContainerResponse::STARTED:
      FX_LOGS(INFO) << "Container started";
      break;
    case vm_tools::tremplin::StartContainerResponse::STARTING:
      FX_LOGS(INFO) << "Container starting";
      break;
    case vm_tools::tremplin::StartContainerResponse::FAILED:
      PostContainerFailure("Failed to start container: " + response.failure_reason());
      break;
    case vm_tools::tremplin::StartContainerResponse::UNKNOWN:
    default:
      PostContainerFailure("Unknown status: " + std::to_string(response.status()));
      break;
  }
}

void Guest::SetupUser() {
  FX_CHECK(tremplin_) << "SetupUser called without a Tremplin connection";
  FX_LOGS(INFO) << "Creating user '" << kDefaultContainerUser << "'...";

  grpc::ClientContext context;
  vm_tools::tremplin::SetUpUserRequest request;
  vm_tools::tremplin::SetUpUserResponse response;

  request.mutable_container_name()->assign(kContainerName);
  request.mutable_container_username()->assign(kDefaultContainerUser);
  {
    TRACE_DURATION("linux_runner", "SetUpUserRPC");
    auto status = tremplin_->SetUpUser(&context, request, &response);
    FX_CHECK(status.ok()) << "Failed to setup user '" << kDefaultContainerUser
                          << "': " << status.error_message();
  }

  switch (response.status()) {
    case vm_tools::tremplin::SetUpUserResponse::EXISTS:
    case vm_tools::tremplin::SetUpUserResponse::SUCCESS:
      FX_LOGS(INFO) << "User created.";
      StartContainer();
      break;
    case vm_tools::tremplin::SetUpUserResponse::FAILED:
      PostContainerFailure("Failed to create user: " + response.failure_reason());
      break;
    case vm_tools::tremplin::SetUpUserResponse::UNKNOWN:
    default:
      PostContainerFailure("Unknown status: " + std::to_string(response.status()));
      break;
  }
}

grpc::Status Guest::VmReady(grpc::ServerContext* context, const vm_tools::EmptyMessage* request,
                            vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::VmReady");
  TRACE_FLOW_END("linux_runner", "TerminaBoot", vm_ready_nonce_);
  FX_LOGS(INFO) << "VM Ready -- Connecting to Maitre'd...";
  auto start_maitred =
      [this](fpromise::result<std::unique_ptr<vm_tools::Maitred::Stub>, zx_status_t>& result) {
        FX_CHECK(result.is_ok()) << "Failed to connect to Maitre'd";
        this->maitred_ = std::move(result.value());
        MountVmTools();
        MountExtrasPartition();
        ConfigureNetwork();
        StartTermina();
      };
  auto task = NewGrpcVsockStub<vm_tools::Maitred>(socket_endpoint_, guest_cid_, kMaitredPort)
                  .then(start_maitred);
  executor_.schedule_task(std::move(task));
  return grpc::Status::OK;
}

grpc::Status Guest::TremplinReady(grpc::ServerContext* context,
                                  const ::vm_tools::tremplin::TremplinStartupInfo* request,
                                  vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::TremplinReady");
  FX_LOGS(INFO) << "Tremplin Ready.";
  auto start_tremplin =
      [this](fpromise::result<std::unique_ptr<vm_tools::tremplin::Tremplin::Stub>, zx_status_t>&
                 result) mutable -> fpromise::result<> {
    FX_CHECK(result.is_ok()) << "Failed to connect to Tremplin";
    tremplin_ = std::move(result.value());
    CreateContainer();
    return fpromise::ok();
  };
  auto task =
      NewGrpcVsockStub<vm_tools::tremplin::Tremplin>(socket_endpoint_, guest_cid_, kTremplinPort)
          .then(start_tremplin);
  executor_.schedule_task(std::move(task));
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateCreateStatus(grpc::ServerContext* context,
                                       const vm_tools::tremplin::ContainerCreationProgress* request,
                                       vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateCreateStatus");
  switch (request->status()) {
    case vm_tools::tremplin::ContainerCreationProgress::CREATED:
      FX_LOGS(INFO) << "Container created: " << request->container_name();
      SetupUser();
      break;
    case vm_tools::tremplin::ContainerCreationProgress::DOWNLOADING:
      PostContainerDownloadProgress(request->download_progress());
      FX_LOGS(INFO) << "Downloading " << request->container_name() << ": "
                    << request->download_progress() << "%";
      if (request->download_progress() >= 100) {
        PostContainerStatus(fuchsia::virtualization::ContainerStatus::EXTRACTING);
        FX_LOGS(INFO) << "Extracting " << request->container_name();
      }
      break;
    case vm_tools::tremplin::ContainerCreationProgress::DOWNLOAD_TIMED_OUT:
      PostContainerFailure("Download timed out");
      break;
    case vm_tools::tremplin::ContainerCreationProgress::CANCELLED:
      PostContainerFailure("Download cancelled");
      break;
    case vm_tools::tremplin::ContainerCreationProgress::FAILED:
      PostContainerFailure("Download failed: " + request->failure_reason());
      break;
    case vm_tools::tremplin::ContainerCreationProgress::UNKNOWN:
    default:
      PostContainerFailure("Unknown download status: " + std::to_string(request->status()));
      break;
  }
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateDeletionStatus(
    ::grpc::ServerContext* context, const ::vm_tools::tremplin::ContainerDeletionProgress* request,
    ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateDeletionStatus");
  FX_LOGS(INFO) << "Update Deletion Status";
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateStartStatus(::grpc::ServerContext* context,
                                      const ::vm_tools::tremplin::ContainerStartProgress* request,
                                      ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateStartStatus");
  FX_LOGS(INFO) << "Update Start Status";
  switch (request->status()) {
    case vm_tools::tremplin::ContainerStartProgress::STARTED:
      FX_LOGS(INFO) << "Container started";
      break;
    default:
      PostContainerFailure("Unknown start status: " + std::to_string(request->status()));
      break;
  }
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateExportStatus(::grpc::ServerContext* context,
                                       const ::vm_tools::tremplin::ContainerExportProgress* request,
                                       ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateExportStatus");
  FX_LOGS(INFO) << "Update Export Status";
  return grpc::Status::OK;
}
grpc::Status Guest::UpdateImportStatus(::grpc::ServerContext* context,
                                       const ::vm_tools::tremplin::ContainerImportProgress* request,
                                       ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateImportStatus");
  FX_LOGS(INFO) << "Update Import Status";
  return grpc::Status::OK;
}
grpc::Status Guest::ContainerShutdown(::grpc::ServerContext* context,
                                      const ::vm_tools::tremplin::ContainerShutdownInfo* request,
                                      ::vm_tools::tremplin::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::ContainerShutdown");
  FX_LOGS(INFO) << "Container Shutdown";
  return grpc::Status::OK;
}

grpc::Status Guest::ContainerReady(grpc::ServerContext* context,
                                   const vm_tools::container::ContainerStartupInfo* request,
                                   vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::ContainerReady");

  // Add Magma GPU support to container.
  AddMagmaDeviceToContainer();
  SetupGPUDriversInContainer();

  // Start required user services.
  LaunchContainerShell();

  // Connect to Garcon service in the container.
  // TODO(tjdetwiler): validate token.
  auto garcon_port = request->garcon_port();
  FX_LOGS(INFO) << "Container Ready; Garcon listening on port " << garcon_port;
  auto start_garcon = [this](fpromise::result<std::unique_ptr<vm_tools::container::Garcon::Stub>,
                                              zx_status_t>& result) mutable -> fpromise::result<> {
    FX_CHECK(result.is_ok()) << "Failed to connect to Garcon";
    garcon_ = std::move(result.value());
    DumpContainerDebugInfo();

    // Container is now Ready.
    PostContainerStatus(fuchsia::virtualization::ContainerStatus::READY);

    return fpromise::ok();
  };
  auto task =
      NewGrpcVsockStub<vm_tools::container::Garcon>(socket_endpoint_, guest_cid_, garcon_port)
          .then(start_garcon);
  executor_.schedule_task(std::move(task));

  return grpc::Status::OK;
}

grpc::Status Guest::ContainerShutdown(grpc::ServerContext* context,
                                      const vm_tools::container::ContainerShutdownInfo* request,
                                      vm_tools::EmptyMessage* response) {
  FX_LOGS(INFO) << "Container Shutdown";
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateApplicationList(
    grpc::ServerContext* context, const vm_tools::container::UpdateApplicationListRequest* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateApplicationList");
  FX_LOGS(INFO) << "Update Application List";
  for (const auto& application : request->application()) {
    FX_LOGS(INFO) << "ID: " << application.desktop_file_id();
    const auto& name = application.name().values().begin();
    if (name != application.name().values().end()) {
      FX_LOGS(INFO) << "\tname:             " << name->value();
    }
    const auto& comment = application.comment().values().begin();
    if (comment != application.comment().values().end()) {
      FX_LOGS(INFO) << "\tcomment:          " << comment->value();
    }
    FX_LOGS(INFO) << "\tno_display:       " << application.no_display();
    FX_LOGS(INFO) << "\tstartup_wm_class: " << application.startup_wm_class();
    FX_LOGS(INFO) << "\tstartup_notify:   " << application.startup_notify();
    FX_LOGS(INFO) << "\tpackage_id:       " << application.package_id();
  }
  return grpc::Status::OK;
}

grpc::Status Guest::OpenUrl(grpc::ServerContext* context,
                            const vm_tools::container::OpenUrlRequest* request,
                            vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::OpenUrl");
  FX_LOGS(INFO) << "Open URL";
  return grpc::Status::OK;
}

grpc::Status Guest::InstallLinuxPackageProgress(
    grpc::ServerContext* context,
    const vm_tools::container::InstallLinuxPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::InstallLinuxPackageProgress");
  FX_LOGS(INFO) << "Install Linux Package Progress";
  return grpc::Status::OK;
}

grpc::Status Guest::UninstallPackageProgress(
    grpc::ServerContext* context, const vm_tools::container::UninstallPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UninstallPackageProgress");
  FX_LOGS(INFO) << "Uninstall Package Progress";
  return grpc::Status::OK;
}

grpc::Status Guest::OpenTerminal(grpc::ServerContext* context,
                                 const vm_tools::container::OpenTerminalRequest* request,
                                 vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::OpenTerminal");
  FX_LOGS(INFO) << "Open Terminal";
  return grpc::Status::OK;
}

grpc::Status Guest::UpdateMimeTypes(grpc::ServerContext* context,
                                    const vm_tools::container::UpdateMimeTypesRequest* request,
                                    vm_tools::EmptyMessage* response) {
  TRACE_DURATION("linux_runner", "Guest::UpdateMimeTypes");
  FX_LOGS(INFO) << "Update Mime Types";
  size_t i = 0;
  for (const auto& pair : request->mime_type_mappings()) {
    FX_LOGS(INFO) << "\t" << pair.first << ": " << pair.second;
    if (++i > 10) {
      FX_LOGS(INFO) << "\t..." << (request->mime_type_mappings_size() - i) << " more.";
      break;
    }
  }
  return grpc::Status::OK;
}

void Guest::DumpContainerDebugInfo() {
  FX_CHECK(garcon_) << "Called DumpContainerDebugInfo without a garcon connection";
  FX_LOGS(INFO) << "Dumping Container Debug Info...";

  grpc::ClientContext context;
  vm_tools::container::GetDebugInformationRequest request;
  vm_tools::container::GetDebugInformationResponse response;

  auto grpc_status = garcon_->GetDebugInformation(&context, request, &response);
  if (!grpc_status.ok()) {
    FX_LOGS(ERROR) << "Failed to read container debug information: " << grpc_status.error_message();
    return;
  }

  FX_LOGS(INFO) << "Container debug information:";
  FX_LOGS(INFO) << response.debug_information();
}

void Guest::PostContainerStatus(fuchsia::virtualization::ContainerStatus container_status) {
  callback_(GuestInfo{
      .cid = guest_cid_,
      .container_status = container_status,
  });
}

void Guest::PostContainerDownloadProgress(int32_t download_progress) {
  callback_(GuestInfo{
      .cid = guest_cid_,
      .container_status = fuchsia::virtualization::ContainerStatus::DOWNLOADING,
      .download_percent = download_progress,
  });
}

void Guest::PostContainerFailure(std::string failure_reason) {
  FX_LOGS(ERROR) << failure_reason;
  callback_(GuestInfo{
      .cid = guest_cid_,
      .container_status = fuchsia::virtualization::ContainerStatus::FAILED,
      .failure_reason = failure_reason,
  });
}

}  // namespace linux_runner
