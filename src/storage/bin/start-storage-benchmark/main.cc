// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/status.h>

#include "src/storage/bin/start-storage-benchmark/block-device.h"
#include "src/storage/bin/start-storage-benchmark/command-line-options.h"
#include "src/storage/bin/start-storage-benchmark/memfs.h"
#include "src/storage/bin/start-storage-benchmark/run-benchmark.h"
#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"

namespace storage_benchmark {

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

zx::status<std::unique_ptr<RunningFilesystem>> StartFilesystem(const CommandLineOptions &options) {
  if (options.filesystem == FilesystemOption::kMemfs) {
    return Memfs::Create();
  }

  auto fvm_block_device_path = FindFvmBlockDevicePath();
  if (fvm_block_device_path.is_error()) {
    return fvm_block_device_path.take_error();
  }

  auto fvm_client = ConnectToFvm(*fvm_block_device_path);
  if (fvm_client.is_error()) {
    return fvm_client.take_error();
  }

  auto fvm_volume = FvmVolume::Create(*fvm_client, options.partition_size);
  if (fvm_volume.is_error()) {
    return fvm_volume.take_error();
  }
  std::string block_device_path = fvm_volume->path();

  if (options.zxcrypt) {
    auto zxcrypt_path = CreateZxcryptVolume(block_device_path);
    if (zxcrypt_path.is_error()) {
      return zxcrypt_path.take_error();
    }
    block_device_path = *zxcrypt_path;
  }

  fs_management::DiskFormat disk_format = FilesystemOptionToDiskFormat(options.filesystem);
  if (zx::status<> status = FormatBlockDevice(block_device_path, disk_format); status.is_error()) {
    return status.take_error();
  }

  return StartBlockDeviceFilesystem(block_device_path, disk_format, *std::move(fvm_volume));
}

zx::status<> Run(const CommandLineOptions &options) {
  auto filesystem = StartFilesystem(options);
  if (filesystem.is_error()) {
    return filesystem.take_error();
  }

  auto filesystem_connection = filesystem->GetFilesystemRoot();
  if (filesystem_connection.is_error()) {
    return filesystem_connection.take_error();
  }

  return RunBenchmark(options.benchmark_url, options.benchmark_options,
                      std::move(filesystem_connection).value(), options.mount_path);
}

}  // namespace storage_benchmark

int main(int argc, char *argv[]) {
  auto options = storage_benchmark::ParseCommandLine(argc, argv);
  if (options.is_error()) {
    fprintf(stderr, "%s\n", options.error_value().c_str());
    return EXIT_FAILURE;
  }

  if (auto result = storage_benchmark::Run(*options); result.is_error()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
