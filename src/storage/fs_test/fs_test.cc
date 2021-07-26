// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/fs_test.h"

#include <dlfcn.h>
#include <errno.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/fs/cpp/fidl.h>
#include <fuchsia/fs/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/hardware/ramdisk/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/service/llcpp/service.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <iostream>
#include <unordered_map>
#include <utility>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/fvm.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/isolated_devmgr/v2_component/bind_devfs_to_namespace.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/fs_test/blobfs_test.h"
#include "src/storage/fs_test/json_filesystem.h"
#include "src/storage/fs_test/test_filesystem.h"
#include "src/storage/testing/fvm.h"

namespace fs_test {
namespace {

// Creates a ram-disk with an optional FVM partition. Returns the ram-disk and the device path.
zx::status<std::pair<storage::RamDisk, std::string>> CreateRamDisk(
    const TestFilesystemOptions& options) {
  if (options.use_ram_nand) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::vmo vmo;
  if (options.vmo->is_valid()) {
    uint64_t vmo_size;
    auto status = zx::make_status(options.vmo->get_size(&vmo_size));
    if (status.is_error()) {
      return status.take_error();
    }
    status = zx::make_status(options.vmo->create_child(ZX_VMO_CHILD_SLICE, 0, vmo_size, &vmo));
    if (status.is_error()) {
      return status.take_error();
    }
  } else {
    fzl::VmoMapper mapper;
    auto status =
        zx::make_status(mapper.CreateAndMap(options.device_block_size * options.device_block_count,
                                            ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
    if (status.is_error()) {
      std::cout << "Unable to create VMO for ramdisk: " << status.status_string() << std::endl;
      return status.take_error();
    }

    // Fill the ram-disk with a non-zero value so that we don't inadvertently depend on it being
    // zero filled.
    if (!options.zero_fill) {
      memset(mapper.start(), 0xaf, mapper.size());
    }
  }

  // Create a ram-disk.
  auto ram_disk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), options.device_block_size);
  if (ram_disk_or.is_error()) {
    return ram_disk_or.take_error();
  }

  if (options.fail_after) {
    if (auto status = ram_disk_or->SleepAfter(options.fail_after); status.is_error()) {
      return status.take_error();
    }
  }

  if (options.ram_disk_discard_random_after_last_flush) {
    ramdisk_set_flags(ram_disk_or->client(),
                      fuchsia_hardware_ramdisk::wire::kRamdiskFlagDiscardRandom |
                          fuchsia_hardware_ramdisk::wire::kRamdiskFlagDiscardNotFlushedOnWake);
  }

  std::string device_path = ram_disk_or.value().path();
  return zx::ok(std::make_pair(std::move(ram_disk_or).value(), std::move(device_path)));
}

// Creates a ram-nand device.  It does not create an FVM partition; that is left to the caller.
zx::status<std::pair<ramdevice_client::RamNand, std::string>> CreateRamNand(
    const TestFilesystemOptions& options) {
  auto status = isolated_devmgr::OneTimeSetUp();
  if (status.is_error()) {
    return status.take_error();
  }

  constexpr int kPageSize = 4096;
  constexpr int kPagesPerBlock = 64;
  constexpr int kOobSize = 8;

  uint32_t block_count;
  zx::vmo vmo;
  if (options.vmo->is_valid()) {
    uint64_t vmo_size;
    status = zx::make_status(options.vmo->get_size(&vmo_size));
    if (status.is_error()) {
      return status.take_error();
    }
    block_count = vmo_size / (kPageSize + kOobSize) / kPagesPerBlock;
    // For now, when using a ram-nand device, the only supported device block size is 8 KiB, so
    // raise an error if the user tries to ask for something different.
    if ((options.device_block_size != 0 && options.device_block_size != 8192) ||
        (options.device_block_count != 0 &&
         options.device_block_size * options.device_block_count !=
             block_count * kPageSize * kPagesPerBlock)) {
      std::cout << "Bad device parameters" << std::endl;
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    status = zx::make_status(options.vmo->create_child(ZX_VMO_CHILD_SLICE, 0, vmo_size, &vmo));
    if (status.is_error()) {
      return status.take_error();
    }
  } else if (options.device_block_size != 8192) {  // FTL exports a device with 8 KiB blocks.
    return zx::error(ZX_ERR_INVALID_ARGS);
  } else {
    block_count =
        options.device_block_size * options.device_block_count / kPageSize / kPagesPerBlock;
  }

  status =
      zx::make_status(wait_for_device("/dev/sys/platform/00:00:2e/nand-ctl", zx::sec(10).get()));
  if (status.is_error()) {
    std::cout << "Failed waiting for /dev/sys/platform/00:00:2e/nand-ctl to appear: "
              << status.status_string() << std::endl;
    return status.take_error();
  }

  std::optional<ramdevice_client::RamNand> ram_nand;
  fuchsia_hardware_nand_RamNandInfo config = {
      .vmo = vmo.release(),
      .nand_info.page_size = kPageSize,
      .nand_info.pages_per_block = kPagesPerBlock,
      .nand_info.num_blocks = block_count,
      .nand_info.ecc_bits = 8,
      .nand_info.oob_size = kOobSize,
      .nand_info.nand_class = fuchsia_hardware_nand_Class_FTL,
      .fail_after = options.fail_after};
  status = zx::make_status(ramdevice_client::RamNand::Create(&config, &ram_nand));
  if (status.is_error()) {
    std::cout << "RamNand::Create failed: " << status.status_string() << std::endl;
    return status.take_error();
  }

  std::string ftl_path = std::string(ram_nand->path()) + "/ftl/block";
  status = zx::make_status(wait_for_device(ftl_path.c_str(), zx::sec(10).get()));
  if (status.is_error()) {
    std::cout << "Timed out waiting for RamNand" << std::endl;
    return status.take_error();
  }
  return zx::ok(std::make_pair(*std::move(ram_nand), std::move(ftl_path)));
}

zx::status<> FsDirectoryAdminUnmount(const std::string& mount_path) {
  // O_ADMIN is not part of the SDK.  Eventually, this should switch to using fs.Admin.
  constexpr int kAdmin = 0x0000'0004;
  int fd = open(mount_path.c_str(), O_DIRECTORY | kAdmin);
  if (fd < 0) {
    std::cout << "Unable to open mount point: " << strerror(errno) << std::endl;
    return zx::error(ZX_ERR_INTERNAL);
  }
  zx_handle_t handle;
  if (auto status = zx::make_status(fdio_get_service_handle(fd, &handle)); status.is_error()) {
    std::cout << "Unable to get service handle: " << status.status_string() << std::endl;
    return status;
  }
  if (auto status =
          zx::make_status(fs::Vfs::UnmountHandle(zx::channel(handle), zx::time::infinite()));
      status.is_error()) {
    std::cout << "Unable to unmount: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

}  // namespace

std::string StripTrailingSlash(const std::string& in) {
  if (!in.empty() && in.back() == '/') {
    return in.substr(0, in.length() - 1);
  } else {
    return in;
  }
}

zx::status<> FsUnbind(const std::string& mount_path) {
  fdio_ns_t* ns;
  if (auto status = zx::make_status(fdio_ns_get_installed(&ns)); status.is_error()) {
    return status;
  }
  if (auto status = zx::make_status(fdio_ns_unbind(ns, StripTrailingSlash(mount_path).c_str()));
      status.is_error()) {
    std::cout << "Unable to unbind: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

// Returns device and device path.
zx::status<std::pair<RamDevice, std::string>> CreateRamDevice(
    const TestFilesystemOptions& options) {
  RamDevice ram_device;
  std::string device_path;

  if (options.use_ram_nand) {
    auto ram_nand_or = CreateRamNand(options);
    if (ram_nand_or.is_error()) {
      return ram_nand_or.take_error();
    }
    auto [ram_nand, nand_device_path] = std::move(ram_nand_or).value();
    ram_device = RamDevice(std::move(ram_nand));
    device_path = std::move(nand_device_path);
  } else {
    auto ram_disk_or = CreateRamDisk(options);
    if (ram_disk_or.is_error()) {
      return ram_disk_or.take_error();
    }
    auto [device, ram_disk_path] = std::move(ram_disk_or).value();
    ram_device = RamDevice(std::move(device));
    device_path = std::move(ram_disk_path);
  }

  // Create an FVM partition if requested.
  if (options.use_fvm) {
    storage::FvmOptions fvm_options = {.initial_fvm_slice_count = options.initial_fvm_slice_count};
    auto fvm_partition_or =
        storage::CreateFvmPartition(device_path, options.fvm_slice_size, fvm_options);
    if (fvm_partition_or.is_error()) {
      return fvm_partition_or.take_error();
    }

    if (options.dummy_fvm_partition_size > 0) {
      auto fvm_fd = fbl::unique_fd(open((device_path + "/fvm").c_str(), O_RDWR));
      if (!fvm_fd) {
        std::cout << "Could not open FVM driver: " << strerror(errno) << std::endl;
        return zx::error(ZX_ERR_BAD_STATE);
      }

      alloc_req_t request = {
          .slice_count = options.dummy_fvm_partition_size / options.fvm_slice_size,
          .type = {0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01,
                   0x02, 0x03, 0x04},
          .guid = {0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01,
                   0x02, 0x03, 0x04},
          .name = "dummy",
      };
      fbl::unique_fd fd(fvm_allocate_partition(fvm_fd.get(), &request));
      if (!fd) {
        std::cout << "Could not allocate dummy FVM partition" << std::endl;
        return zx::error(ZX_ERR_BAD_STATE);
      }
    }

    return zx::ok(std::make_pair(std::move(ram_device), std::move(fvm_partition_or).value()));
  } else {
    return zx::ok(std::make_pair(std::move(ram_device), std::move(device_path)));
  }
}

zx::status<> FsFormat(const std::string& device_path, disk_format_t format,
                      const mkfs_options_t& options) {
  auto status = zx::make_status(mkfs(device_path.c_str(), format, launch_stdio_sync, &options));
  if (status.is_error()) {
    std::cout << "Could not format " << disk_format_string(format)
              << " file system: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

zx::status<> FsMount(const std::string& device_path, const std::string& mount_path,
                     disk_format_t format, const mount_options_t& mount_options,
                     zx::channel* outgoing_directory) {
  auto fd = fbl::unique_fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    std::cout << "Could not open device: " << device_path << ": errno=" << errno << std::endl;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  mount_options_t options = mount_options;
  options.bind_to_namespace = true;
  if (outgoing_directory) {
    zx::channel server;
    auto status = zx::make_status(zx::channel::create(0, outgoing_directory, &server));
    if (status.is_error()) {
      std::cout << "Unable to create channel for outgoing directory: " << status.status_string()
                << std::endl;
      return status;
    }
    options.outgoing_directory.client = outgoing_directory->get();
    options.outgoing_directory.server = server.release();
  }

  // Uncomment the following line to force an fsck at the end of every transaction (where
  // supported).
  // options.fsck_after_every_transaction = true;

  // |fd| is consumed by mount.
  auto status = zx::make_status(mount(fd.release(), StripTrailingSlash(mount_path).c_str(), format,
                                      &options, launch_stdio_async));
  if (status.is_error()) {
    std::cout << "Could not mount " << disk_format_string(format)
              << " file system: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

zx::status<> FsAdminUnmount(const std::string& mount_path, const zx::channel& outgoing_directory) {
  // Detach from the namespace.
  if (auto status = FsUnbind(mount_path); status.is_error()) {
    return status;
  }

  // Now shut down the filesystem.
  fidl::SynchronousInterfacePtr<fuchsia::fs::Admin> admin;
  std::string service_name = std::string("svc/") + fuchsia::fs::Admin::Name_;
  auto status = zx::make_status(fdio_service_connect_at(
      outgoing_directory.get(), service_name.c_str(), admin.NewRequest().TakeChannel().get()));
  if (status.is_error()) {
    std::cout << "Unable to connect to admin service: " << status.status_string() << std::endl;
    return status;
  }
  status = zx::make_status(admin->Shutdown());
  if (status.is_error()) {
    std::cout << "Shut down failed: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

// Returns device and device path.
zx::status<std::pair<RamDevice, std::string>> OpenRamDevice(const TestFilesystemOptions& options) {
  if (!options.vmo->is_valid()) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  RamDevice ram_device;
  std::string device_path;

  if (options.use_ram_nand) {
    // First create the ram-nand device.
    auto ram_nand_or = CreateRamNand(options);
    if (ram_nand_or.is_error()) {
      return ram_nand_or.take_error();
    }
    auto [ram_nand, ftl_device_path] = std::move(ram_nand_or).value();
    ram_device = RamDevice(std::move(ram_nand));
    device_path = std::move(ftl_device_path);
  } else {
    auto ram_disk_or = CreateRamDisk(options);
    if (ram_disk_or.is_error()) {
      std::cout << "Unable to create ram-disk" << std::endl;
    }

    auto [device, ram_disk_path] = std::move(ram_disk_or).value();
    ram_device = RamDevice(std::move(device));
    device_path = std::move(ram_disk_path);
  }

  if (options.use_fvm) {
    // Now bind FVM to it.
    fbl::unique_fd ftl_device(open(device_path.c_str(), O_RDWR));
    if (!ftl_device)
      return zx::error(ZX_ERR_BAD_STATE);
    auto status = storage::BindFvm(ftl_device.get());
    if (status.is_error()) {
      std::cout << "Unable to bind FVM: " << status.status_string() << std::endl;
      return status.take_error();
    }

    device_path.append("/fvm/fs-test-partition-p-1/block");
  }

  auto status = zx::make_status(wait_for_device(device_path.c_str(), zx::sec(10).get()));
  if (status.is_error()) {
    std::cout << "Timed out waiting for partition to show up" << std::endl;
    return status.take_error();
  }

  return zx::ok(std::make_pair(std::move(ram_device), std::move(device_path)));
}

TestFilesystemOptions TestFilesystemOptions::DefaultBlobfs() {
  return TestFilesystemOptions{.description = "Blobfs",
                               .use_fvm = true,
                               .device_block_size = 512,
                               .device_block_count = 196'608,
                               .fvm_slice_size = 32'768,
                               .num_inodes = 512,  // blobfs can grow as needed.
                               .filesystem = &BlobfsFilesystem::SharedInstance()};
}

TestFilesystemOptions TestFilesystemOptions::BlobfsWithoutFvm() {
  TestFilesystemOptions blobfs_with_no_fvm = TestFilesystemOptions::DefaultBlobfs();
  blobfs_with_no_fvm.description = "BlobfsWithoutFvm";
  blobfs_with_no_fvm.use_fvm = false;
  blobfs_with_no_fvm.num_inodes = 2048;
  return blobfs_with_no_fvm;
}

std::ostream& operator<<(std::ostream& out, const TestFilesystemOptions& options) {
  return out << options.description;
}

std::vector<TestFilesystemOptions> AllTestFilesystems() {
  static const std::vector<TestFilesystemOptions>* options = [] {
    const char kConfigFile[] = "/pkg/config/config.json";
    json_parser::JSONParser parser;
    auto config = parser.ParseFromFile(std::string(kConfigFile));
    auto iter = config.FindMember("library");
    std::unique_ptr<Filesystem> filesystem;
    if (iter != config.MemberEnd()) {
      void* handle = dlopen(iter->value.GetString(), RTLD_NOW);
      FX_CHECK(handle) << dlerror();
      auto get_filesystem =
          reinterpret_cast<std::unique_ptr<Filesystem> (*)()>(dlsym(handle, "_Z13GetFilesystemv"));
      FX_CHECK(get_filesystem) << dlerror();
      filesystem = get_filesystem();
    } else {
      filesystem = JsonFilesystem::NewFilesystem(config).value();
    }
    std::string name = config["name"].GetString();
    auto options = new std::vector<TestFilesystemOptions>;
    iter = config.FindMember("options");
    if (iter == config.MemberEnd()) {
      name[0] = toupper(name[0]);
      options->push_back(TestFilesystemOptions{.description = name,
                                               .use_fvm = false,
                                               .device_block_size = 512,
                                               .device_block_count = 196'608,
                                               .filesystem = filesystem.get()});
    } else {
      for (size_t i = 0; i < iter->value.Size(); ++i) {
        const auto& opt = iter->value[i];
        options->push_back(TestFilesystemOptions{.description = opt["description"].GetString(),
                                                 .use_fvm = opt["use_fvm"].GetBool(),
                                                 .device_block_size = 512,
                                                 .device_block_count = 196'608,
                                                 .fvm_slice_size = 32'768,
                                                 .filesystem = filesystem.get()});
      }
    }
    filesystem.release();  // Deliberate leak
    return options;
  }();

  return *options;
}

TestFilesystemOptions OptionsWithDescription(std::string_view description) {
  for (const auto& options : AllTestFilesystems()) {
    if (options.description == description) {
      return options;
    }
  }
  FX_LOGS(FATAL) << "No test options with description: " << description;
  abort();
}

std::vector<TestFilesystemOptions> MapAndFilterAllTestFilesystems(
    std::function<std::optional<TestFilesystemOptions>(const TestFilesystemOptions&)>
        map_and_filter) {
  std::vector<TestFilesystemOptions> results;
  for (const TestFilesystemOptions& options : AllTestFilesystems()) {
    auto r = map_and_filter(options);
    if (r) {
      results.push_back(*std::move(r));
    }
  }
  return results;
}

// -- FilesystemInstance --

// Default implementation
zx::status<> FilesystemInstance::Unmount(const std::string& mount_path) {
  if (auto status = FsDirectoryAdminUnmount(mount_path); status.is_error()) {
    return status;
  }
  return FsUnbind(mount_path);
}

// -- Blobfs --

class BlobfsInstance : public FilesystemInstance {
 public:
  BlobfsInstance(RamDevice device, std::string device_path)
      : device_(std::move(device)), device_path_(std::move(device_path)) {}

  zx::status<> Format(const TestFilesystemOptions& options) override {
    mkfs_options_t mkfs_options = default_mkfs_options;
    if (options.blob_layout_format) {
      mkfs_options.blob_layout_format =
          blobfs::GetBlobLayoutFormatCommandLineArg(options.blob_layout_format.value());
    }
    mkfs_options.num_inodes = options.num_inodes;
    return FsFormat(device_path_, DISK_FORMAT_BLOBFS, mkfs_options);
  }

  zx::status<> Mount(const std::string& mount_path, const mount_options_t& options) override {
    return FsMount(device_path_, mount_path, DISK_FORMAT_BLOBFS, options, &outgoing_directory_);
  }

  zx::status<> Unmount(const std::string& mount_path) override {
    outgoing_directory_.reset();
    return FilesystemInstance::Unmount(mount_path);
  }

  zx::status<> Fsck() override {
    fsck_options_t options{
        .verbose = false,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    return zx::make_status(
        fsck(device_path_.c_str(), DISK_FORMAT_BLOBFS, &options, launch_stdio_sync));
  }

  zx::status<std::string> DevicePath() const override { return zx::ok(std::string(device_path_)); }
  storage::RamDisk* GetRamDisk() override { return std::get_if<storage::RamDisk>(&device_); }
  ramdevice_client::RamNand* GetRamNand() override {
    return std::get_if<ramdevice_client::RamNand>(&device_);
  }
  zx::unowned_channel GetOutgoingDirectory() const override { return outgoing_directory_.borrow(); }

 private:
  RamDevice device_;
  std::string device_path_;
  zx::channel outgoing_directory_;
};

std::unique_ptr<FilesystemInstance> BlobfsFilesystem::Create(RamDevice device,
                                                             std::string device_path) const {
  return std::make_unique<BlobfsInstance>(std::move(device), std::move(device_path));
}

zx::status<std::unique_ptr<FilesystemInstance>> BlobfsFilesystem::Open(
    const TestFilesystemOptions& options) const {
  auto result = OpenRamDevice(options);
  if (result.is_error()) {
    return result.take_error();
  }
  auto [ram_nand, device_path] = std::move(result).value();
  return zx::ok(std::make_unique<BlobfsInstance>(std::move(ram_nand), std::move(device_path)));
}

// --

zx::status<TestFilesystem> TestFilesystem::FromInstance(
    const TestFilesystemOptions& options, std::unique_ptr<FilesystemInstance> instance) {
  static uint32_t mount_index;
  TestFilesystem filesystem(options, std::move(instance),
                            std::string("/fs_test." + std::to_string(mount_index++) + "/"));
  auto status = filesystem.Mount();
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(filesystem));
}

zx::status<TestFilesystem> TestFilesystem::Create(const TestFilesystemOptions& options) {
  auto instance_or = options.filesystem->Make(options);
  if (instance_or.is_error()) {
    return instance_or.take_error();
  }
  return FromInstance(options, std::move(instance_or).value());
}

zx::status<TestFilesystem> TestFilesystem::Open(const TestFilesystemOptions& options) {
  auto instance_or = options.filesystem->Open(options);
  if (instance_or.is_error()) {
    return instance_or.take_error();
  }
  return FromInstance(options, std::move(instance_or).value());
}

TestFilesystem::~TestFilesystem() {
  if (filesystem_) {
    if (mounted_) {
      auto status = Unmount();
      if (status.is_error()) {
        std::cout << "warning: failed to unmount: " << status.status_string() << std::endl;
      }
    }
    rmdir(mount_path_.c_str());
  }
}

zx::status<> TestFilesystem::MountWithOptions(const mount_options_t& options) {
  auto status = filesystem_->Mount(mount_path_, options);
  if (status.is_ok()) {
    mounted_ = true;
  }
  return status;
}

zx::status<> TestFilesystem::Unmount() {
  if (!filesystem_) {
    return zx::ok();
  }
  auto status = filesystem_->Unmount(mount_path_);
  if (status.is_ok()) {
    mounted_ = false;
  }
  return status;
}

zx::status<> TestFilesystem::Fsck() { return filesystem_->Fsck(); }

zx::status<std::string> TestFilesystem::DevicePath() const { return filesystem_->DevicePath(); }

fidl::ClientEnd<fuchsia_io::Directory> TestFilesystem::GetSvcDirectory() const {
  // Get the svc directory for the test filesystem to connect to fuchsia.fs.Query.
  fidl::UnownedClientEnd<fuchsia_io::Directory> fs_outgoing(GetOutgoingDirectory());
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    std::cout << "warning: failed to create svc handles" << status;
    return zx::channel();
  }
  if (!fidl::WireCall(fs_outgoing)
           .Open(fuchsia_io::wire::kOpenFlagDirectory | fuchsia_io::wire::kOpenRightReadable |
                     fuchsia_io::wire::kOpenRightWritable,
                 0, "svc", std::move(server))
           .ok()) {
    std::cout << "warning: Open failed";
    return zx::channel();
  }
  return fidl::ClientEnd<fuchsia_io::Directory>(std::move(client));
}

zx::status<uint64_t> TestFilesystem::GetFsInfoTotalBytes() const {
  auto svc = GetSvcDirectory();
  auto client_end = service::ConnectAt<fuchsia_fs::Query>(svc);
  if (client_end.is_error())
    return client_end.take_error();
  auto result = fidl::WireCall(*client_end).GetInfo(fuchsia_fs::wire::FilesystemInfoQuery::kMask);
  if (!result.ok())
    return zx::error(result.status());  // Transport error.
  if (result->result.is_err())
    return zx::error(result->result.err());  // Domain specific error.
  return zx::ok(result->result.response().info.total_bytes());
}

zx::status<uint64_t> TestFilesystem::GetFsInfoUsedBytes() const {
  auto svc = GetSvcDirectory();
  auto client_end = service::ConnectAt<fuchsia_fs::Query>(svc);
  if (client_end.is_error())
    return client_end.take_error();
  auto result = fidl::WireCall(*client_end).GetInfo(fuchsia_fs::wire::FilesystemInfoQuery::kMask);
  if (!result.ok())
    return zx::error(result.status());  // Transport error.
  if (result->result.is_err())
    return zx::error(result->result.err());  // Domain specific error.
  return zx::ok(result->result.response().info.used_bytes());
}

}  // namespace fs_test
