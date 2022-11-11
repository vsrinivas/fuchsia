// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/fs_test.h"

#include <dlfcn.h>
#include <errno.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.ramdisk/cpp/wire.h>
#include <fuchsia/fs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <utility>

#include <fbl/unique_fd.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/mkfs_with_default.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/fs_test/blobfs_test.h"
#include "src/storage/fs_test/json_filesystem.h"
#include "src/storage/testing/fvm.h"

namespace fs_test {
namespace {

/// Amount of time to wait for a given device to be available.
constexpr zx_duration_t kDeviceWaitTime = zx::sec(30).get();

// Creates a ram-disk with an optional FVM partition. Returns the ram-disk and the device path.
zx::result<std::pair<storage::RamDisk, std::string>> CreateRamDisk(
    const TestFilesystemOptions& options) {
  if (options.use_ram_nand) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  zx::vmo vmo;
  if (options.vmo->is_valid()) {
    uint64_t vmo_size;
    auto status = zx::make_result(options.vmo->get_size(&vmo_size));
    if (status.is_error()) {
      return status.take_error();
    }
    status = zx::make_result(options.vmo->create_child(ZX_VMO_CHILD_SLICE, 0, vmo_size, &vmo));
    if (status.is_error()) {
      return status.take_error();
    }
  } else {
    fzl::VmoMapper mapper;
    auto status =
        zx::make_result(mapper.CreateAndMap(options.device_block_size * options.device_block_count,
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
zx::result<std::pair<ramdevice_client::RamNand, std::string>> CreateRamNand(
    const TestFilesystemOptions& options) {
  constexpr int kPageSize = 4096;
  constexpr int kPagesPerBlock = 64;
  constexpr int kOobSize = 8;

  uint32_t block_count;
  zx::vmo vmo;
  if (options.vmo->is_valid()) {
    uint64_t vmo_size;
    auto status = zx::make_result(options.vmo->get_size(&vmo_size));
    if (status.is_error()) {
      return status.take_error();
    }
    block_count = static_cast<uint32_t>(vmo_size / (kPageSize + kOobSize) / kPagesPerBlock);
    // For now, when using a ram-nand device, the only supported device block size is 8 KiB, so
    // raise an error if the user tries to ask for something different.
    if ((options.device_block_size != 0 && options.device_block_size != 8192) ||
        (options.device_block_count != 0 &&
         options.device_block_size * options.device_block_count !=
             block_count * kPageSize * kPagesPerBlock)) {
      std::cout << "Bad device parameters" << std::endl;
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    status = zx::make_result(options.vmo->create_child(ZX_VMO_CHILD_SLICE, 0, vmo_size, &vmo));
    if (status.is_error()) {
      return status.take_error();
    }
  } else if (options.device_block_size != 8192) {  // FTL exports a device with 8 KiB blocks.
    return zx::error(ZX_ERR_INVALID_ARGS);
  } else {
    block_count = static_cast<uint32_t>(options.device_block_size * options.device_block_count /
                                        kPageSize / kPagesPerBlock);
  }

  auto status =
      zx::make_result(wait_for_device("/dev/sys/platform/00:00:2e/nand-ctl", kDeviceWaitTime));
  if (status.is_error()) {
    std::cout << "Failed waiting for /dev/sys/platform/00:00:2e/nand-ctl to appear: "
              << status.status_string() << std::endl;
    return status.take_error();
  }

  std::optional<ramdevice_client::RamNand> ram_nand;
  fuchsia_hardware_nand::wire::RamNandInfo config = {
      .vmo = std::move(vmo),
      .nand_info =
          {
              .page_size = kPageSize,
              .pages_per_block = kPagesPerBlock,
              .num_blocks = block_count,
              .ecc_bits = 8,
              .oob_size = kOobSize,
              .nand_class = fuchsia_hardware_nand::wire::Class::kFtl,
          },
      .fail_after = options.fail_after,
  };
  status = zx::make_result(ramdevice_client::RamNand::Create(std::move(config), &ram_nand));
  if (status.is_error()) {
    std::cout << "RamNand::Create failed: " << status.status_string() << std::endl;
    return status.take_error();
  }

  std::string ftl_path = std::string(ram_nand->path()) + "/ftl/block";
  status = zx::make_result(wait_for_device(ftl_path.c_str(), kDeviceWaitTime));
  if (status.is_error()) {
    std::cout << "Timed out waiting for RamNand" << std::endl;
    return status.take_error();
  }
  return zx::ok(std::make_pair(*std::move(ram_nand), std::move(ftl_path)));
}

}  // namespace

std::string StripTrailingSlash(const std::string& in) {
  if (!in.empty() && in.back() == '/') {
    return in.substr(0, in.length() - 1);
  }
  return in;
}

zx::result<> FsUnbind(const std::string& mount_path) {
  fdio_ns_t* ns;
  if (auto status = zx::make_result(fdio_ns_get_installed(&ns)); status.is_error()) {
    return status;
  }
  if (auto status = zx::make_result(fdio_ns_unbind(ns, StripTrailingSlash(mount_path).c_str()));
      status.is_error()) {
    std::cout << "Unable to unbind: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

// Returns device and device path.
zx::result<std::pair<RamDevice, std::string>> CreateRamDevice(
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
    auto fvm_partition_or = storage::CreateFvmPartition(
        device_path, static_cast<int>(options.fvm_slice_size), fvm_options);
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
      if (fs_management::FvmAllocatePartition(fvm_fd.get(), request).is_error()) {
        std::cout << "Could not allocate dummy FVM partition" << std::endl;
        return zx::error(ZX_ERR_BAD_STATE);
      }
    }

    return zx::ok(std::make_pair(std::move(ram_device), std::move(fvm_partition_or).value()));
  }
  return zx::ok(std::make_pair(std::move(ram_device), std::move(device_path)));
}

zx::result<> FsFormat(const std::string& device_path, fs_management::DiskFormat format,
                      const fs_management::MkfsOptions& options, bool create_default_volume) {
  zx::result<> status;
  if (create_default_volume) {
    auto crypt_client = GetCryptService();
    if (crypt_client.is_error())
      return crypt_client.take_error();
    status =
        fs_management::MkfsWithDefault(device_path.c_str(), format, fs_management::LaunchStdioSync,
                                       options, *std::move(crypt_client));
  } else {
    status = zx::make_result(
        fs_management::Mkfs(device_path.c_str(), format, fs_management::LaunchStdioSync, options));
  }
  if (status.is_error()) {
    std::cout << "Could not format " << fs_management::DiskFormatString(format)
              << " file system: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

zx::result<std::pair<std::unique_ptr<fs_management::SingleVolumeFilesystemInterface>,
                     fs_management::NamespaceBinding>>
FsMount(const std::string& device_path, const std::string& mount_path,
        fs_management::DiskFormat format, const fs_management::MountOptions& options,
        bool is_multi_volume) {
  auto fd = fbl::unique_fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    std::cout << "Could not open device: " << device_path << ": errno=" << errno << std::endl;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  // Uncomment the following line to force an fsck at the end of every transaction (where
  // supported).
  // options.fsck_after_every_transaction = true;

  auto LogMountError = [&format](const auto& error) {
    std::cout << "Could not mount " << fs_management::DiskFormatString(format)
              << " file system: " << error.status_string() << std::endl;
  };

  std::unique_ptr<fs_management::SingleVolumeFilesystemInterface> fs;
  if (is_multi_volume) {
    auto result = fs_management::MountMultiVolumeWithDefault(
        std::move(fd), format, options, fs_management::LaunchStdioAsync, kDefaultVolumeName);
    if (result.is_error()) {
      LogMountError(result);
      return result.take_error();
    }
    fs = std::make_unique<fs_management::StartedSingleVolumeMultiVolumeFilesystem>(
        std::move(*result));
  } else {
    auto result =
        fs_management::Mount(std::move(fd), format, options, fs_management::LaunchStdioAsync);
    if (result.is_error()) {
      LogMountError(result);
      return result.take_error();
    }
    fs = std::make_unique<fs_management::StartedSingleVolumeFilesystem>(std::move(*result));
  }
  auto data = fs->DataRoot();
  if (data.is_error()) {
    LogMountError(data);
    return data.take_error();
  }
  auto binding = fs_management::NamespaceBinding::Create(mount_path.c_str(), std::move(*data));
  if (binding.is_error()) {
    LogMountError(binding);
    return binding.take_error();
  }
  return zx::ok(std::make_pair(std::move(fs), std::move(*binding)));
}

// Returns device and device path.
zx::result<std::pair<RamDevice, std::string>> OpenRamDevice(const TestFilesystemOptions& options) {
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
    zx::result device = component::Connect<fuchsia_device::Controller>(device_path.c_str());
    if (device.is_error()) {
      return device.take_error();
    }
    auto status = storage::BindFvm(device.value());
    if (status.is_error()) {
      std::cout << "Unable to bind FVM: " << status.status_string() << std::endl;
      return status.take_error();
    }

    device_path.append("/fvm/fs-test-partition-p-1/block");
  }

  auto status = zx::make_result(wait_for_device(device_path.c_str(), kDeviceWaitTime));
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
      name[0] = static_cast<char>(toupper(name[0]));
      options->push_back(TestFilesystemOptions{.description = name,
                                               .use_fvm = false,
                                               .device_block_size = 512,
                                               .device_block_count = 196'608,
                                               .filesystem = filesystem.get()});
    } else {
      for (rapidjson::SizeType i = 0; i < iter->value.Size(); ++i) {
        const auto& opt = iter->value[i];
        options->push_back(TestFilesystemOptions{
            .description = opt["description"].GetString(),
            .use_fvm = opt["use_fvm"].GetBool(),
            .has_min_volume_size = ConfigGetOrDefault<bool>(opt, "has_min_volume_size", false),
            .device_block_size = ConfigGetOrDefault<uint64_t>(opt, "device_block_size", 512),
            .device_block_count = ConfigGetOrDefault<uint64_t>(opt, "device_block_count", 196'608),
            .fvm_slice_size = 32'768,
            .filesystem = filesystem.get()});
      }
    }
    __UNUSED Filesystem* fs = filesystem.release();  // Deliberate leak
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
    const std::function<std::optional<TestFilesystemOptions>(const TestFilesystemOptions&)>&
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
zx::result<> FilesystemInstance::Unmount(const std::string& mount_path) {
  // Detach from the namespace.
  if (auto status = FsUnbind(mount_path); status.is_error()) {
    std::cerr << "FsUnbind failed: " << status.status_string() << std::endl;
    return status;
  }

  auto filesystem = fs();
  if (!filesystem) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  auto status = filesystem->Unmount();
  if (status.is_error()) {
    std::cerr << "Shut down failed: " << status.status_string() << std::endl;
    return status;
  }
  return zx::ok();
}

// -- Blobfs --

class BlobfsInstance : public FilesystemInstance {
 public:
  BlobfsInstance(RamDevice device, std::string device_path)
      : device_(std::move(device)), device_path_(std::move(device_path)) {}

  zx::result<> Format(const TestFilesystemOptions& options) override {
    fs_management::MkfsOptions mkfs_options;
    mkfs_options.deprecated_padded_blobfs_format =
        options.blob_layout_format == blobfs::BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
    mkfs_options.num_inodes = options.num_inodes;
    return FsFormat(device_path_, fs_management::kDiskFormatBlobfs, mkfs_options,
                    /*create_default_volume=*/false);
  }

  zx::result<> Mount(const std::string& mount_path,
                     const fs_management::MountOptions& options) override {
    auto res = FsMount(device_path_, mount_path, fs_management::kDiskFormatBlobfs, options,
                       /*is_multi_volume=*/false);
    if (res.is_error())
      return res.take_error();
    fs_ = std::move(res->first);
    binding_ = std::move(res->second);
    return zx::ok();
  }

  zx::result<> Fsck() override {
    fs_management::FsckOptions options{
        .verbose = false,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    return zx::make_result(fs_management::Fsck(device_path_.c_str(),
                                               fs_management::kDiskFormatBlobfs, options,
                                               fs_management::LaunchStdioSync));
  }

  zx::result<std::string> DevicePath() const override { return zx::ok(std::string(device_path_)); }
  storage::RamDisk* GetRamDisk() override { return std::get_if<storage::RamDisk>(&device_); }
  ramdevice_client::RamNand* GetRamNand() override {
    return std::get_if<ramdevice_client::RamNand>(&device_);
  }
  fs_management::SingleVolumeFilesystemInterface* fs() override { return fs_.get(); }
  fidl::UnownedClientEnd<fuchsia_io::Directory> ServiceDirectory() const override {
    return fs_->ExportRoot();
  }
  void Reset() override {
    binding_.Reset();
    fs_.reset();
  }

 private:
  RamDevice device_;
  std::string device_path_;
  std::unique_ptr<fs_management::SingleVolumeFilesystemInterface> fs_;
  fs_management::NamespaceBinding binding_;
};

std::unique_ptr<FilesystemInstance> BlobfsFilesystem::Create(RamDevice device,
                                                             std::string device_path) const {
  return std::make_unique<BlobfsInstance>(std::move(device), std::move(device_path));
}

zx::result<std::unique_ptr<FilesystemInstance>> BlobfsFilesystem::Open(
    const TestFilesystemOptions& options) const {
  auto result = OpenRamDevice(options);
  if (result.is_error()) {
    return result.take_error();
  }
  auto [ram_nand, device_path] = std::move(result).value();
  return zx::ok(std::make_unique<BlobfsInstance>(std::move(ram_nand), std::move(device_path)));
}

}  // namespace fs_test
