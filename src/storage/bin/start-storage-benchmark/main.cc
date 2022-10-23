// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/result.h>

#include <iostream>

#include "src/lib/fxl/test/test_settings.h"
#include "src/storage/bin/start-storage-benchmark/block-device.h"
#include "src/storage/bin/start-storage-benchmark/command-line-options.h"
#include "src/storage/bin/start-storage-benchmark/memfs.h"
#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"

extern "C" bool run_odu_test(const char* const* args);

namespace storage_benchmark {
namespace {

fs_management::DiskFormat FilesystemOptionToDiskFormat(FilesystemOption filesystem) {
  switch (filesystem) {
    case FilesystemOption::kUnset:
      ZX_PANIC("No filesystem specified");
    case FilesystemOption::kMemfs:
      ZX_PANIC("Memfs does not have a disk format");
    case FilesystemOption::kMinfs:
      return fs_management::DiskFormat::kDiskFormatMinfs;
    case FilesystemOption::kFxfs:
      return fs_management::DiskFormat::kDiskFormatFxfs;
    case FilesystemOption::kF2fs:
      return fs_management::DiskFormat::kDiskFormatF2fs;
  }
}

zx::result<std::unique_ptr<RunningFilesystem>> StartFilesystem(const CommandLineOptions& options) {
  if (options.filesystem == FilesystemOption::kMemfs) {
    return Memfs::Create();
  }

  auto fvm_block_device_path = FindFvmBlockDevicePath();
  if (fvm_block_device_path.is_error()) {
    FX_LOGS(ERROR) << "Unable to find FVM device";
    return fvm_block_device_path.take_error();
  }

  auto fvm_client = ConnectToFvm(*fvm_block_device_path);
  if (fvm_client.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to FVM: " << fvm_client.status_string();
    return fvm_client.take_error();
  }

  auto fvm_volume = FvmVolume::Create(*fvm_client, options.partition_size);
  if (fvm_volume.is_error()) {
    FX_LOGS(ERROR) << "Unable to create FVM volume: " << fvm_volume.status_string();
    return fvm_volume.take_error();
  }
  std::string block_device_path = fvm_volume->path();

  if (options.zxcrypt) {
    auto zxcrypt_path = CreateZxcryptVolume(block_device_path);
    if (zxcrypt_path.is_error()) {
      FX_LOGS(ERROR) << "Unable to create zxcrypt volume: " << zxcrypt_path.status_string();
      return zxcrypt_path.take_error();
    }
    block_device_path = *zxcrypt_path;
  }

  fs_management::DiskFormat disk_format = FilesystemOptionToDiskFormat(options.filesystem);
  if (zx::result<> status = FormatBlockDevice(block_device_path, disk_format); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to format device: " << status.status_string();
    return status.take_error();
  }

  return StartBlockDeviceFilesystem(block_device_path, disk_format, *std::move(fvm_volume));
}

int Run(const fxl::CommandLine& command_line) {
  // Mark this process as critical to the job so that the runner knows when we're done whilst
  // children might be running.
  zx::job::default_job()->set_critical(0, *zx::process::self());

  if (!fxl::SetTestSettings(command_line)) {
    std::cerr << "Failed to set test settings" << std::endl;
    return EXIT_FAILURE;
  }

  auto options = storage_benchmark::ParseCommandLine(command_line);
  if (options.is_error()) {
    std::cerr << "Failed to parse command line: " << options.error_value() << std::endl;
    return EXIT_FAILURE;
  }

  auto filesystem = StartFilesystem(*options);
  if (filesystem.is_error()) {
    std::cerr << "Failed to start filesystem: " << filesystem.status_string() << std::endl;
    return EXIT_FAILURE;
  }

  auto filesystem_connection = filesystem->GetFilesystemRoot();
  if (filesystem_connection.is_error()) {
    std::cerr << "Unable to get filesystem root: " << filesystem_connection.status_string()
              << std::endl;
    return EXIT_FAILURE;
  }

  fdio_ns_t* ns;
  if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK) {
    std::cerr << "Unable to get installed namespace: " << zx_status_get_string(status) << std::endl;
    return EXIT_FAILURE;
  }

  if (zx_status_t status = fdio_ns_bind(ns, options->mount_path.c_str(),
                                        filesystem_connection->TakeChannel().release());
      status != ZX_OK) {
    std::cerr << "Unable to bind " << options->mount_path
              << " to namespace: " << zx_status_get_string(status) << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<const char*> args;
  for (const auto& arg : options->benchmark_options) {
    args.push_back(arg.c_str());
  }
  args.push_back(nullptr);
  if (!run_odu_test(args.data())) {
    std::cerr << "run_odu_test failed" << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}  // namespace
}  // namespace storage_benchmark

int main(int argc, char* argv[]) {
  return storage_benchmark::Run(fxl::CommandLineFromArgcArgv(argc, argv));
}
