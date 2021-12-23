// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/service/startup.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/mkfs.h"

namespace blobfs {
namespace {

MountOptions ParseMountOptions(fuchsia_fs_startup::wire::StartOptions start_options) {
  MountOptions options;

  options.verbose = start_options.verbose;
  options.metrics = start_options.collect_metrics;
  options.sandbox_decompression = start_options.sandbox_decompression;

  if (start_options.read_only) {
    options.writability = Writability::ReadOnlyFilesystem;
  }
  if (start_options.write_compression_level >= 0) {
    options.compression_settings.compression_level = start_options.write_compression_level;
  }

  switch (start_options.write_compression_algorithm) {
    case fuchsia_fs_startup::wire::CompressionAlgorithm::kZstdChunked:
      options.compression_settings.compression_algorithm = CompressionAlgorithm::kChunked;
      break;
    case fuchsia_fs_startup::wire::CompressionAlgorithm::kUncompressed:
      options.compression_settings.compression_algorithm = CompressionAlgorithm::kUncompressed;
      break;
  }

  switch (start_options.cache_eviction_policy_override) {
    case fuchsia_fs_startup::wire::EvictionPolicyOverride::kNone:
      options.pager_backed_cache_policy = std::nullopt;
      break;
    case fuchsia_fs_startup::wire::EvictionPolicyOverride::kNeverEvict:
      options.pager_backed_cache_policy = CachePolicy::NeverEvict;
      break;
    case fuchsia_fs_startup::wire::EvictionPolicyOverride::kEvictImmediately:
      options.pager_backed_cache_policy = CachePolicy::EvictImmediately;
      break;
  }

  return options;
}

FilesystemOptions ParseFormatOptions(fuchsia_fs_startup::wire::FormatOptions format_options) {
  FilesystemOptions options;

  if (format_options.num_inodes > 0) {
    options.num_inodes = format_options.num_inodes;
  }
  if (format_options.deprecated_padded_blobfs_format) {
    options.blob_layout_format = BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
  }

  return options;
}

}  // namespace

StartupService::StartupService(async_dispatcher_t* dispatcher, ConfigureCallback cb)
    : fs::Service([dispatcher, this](fidl::ServerEnd<fuchsia_fs_startup::Startup> server_end) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
      }),
      configure_(std::move(cb)) {}

void StartupService::Start(StartRequestView request, StartCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    completer.ReplyError(status);
  }
  zx::status<> res = configure_(std::move(device), ParseMountOptions(request->options));
  if (res.is_error()) {
    completer.ReplyError(res.status_value());
    return;
  }
  completer.ReplySuccess();
}

void StartupService::Format(FormatRequestView request, FormatCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device: " << zx_status_get_string(status);
    completer.ReplyError(status);
  }
  status = FormatFilesystem(device.get(), ParseFormatOptions(std::move(request->options)));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to format blobfs: " << zx_status_get_string(status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void StartupService::Check(CheckRequestView request, CheckCompleter::Sync& completer) {
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status =
      block_client::RemoteBlockDevice::Create(request->device.TakeChannel(), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    completer.ReplyError(status);
  }
  // Blobfs supports none of the check options.
  MountOptions options;
  status = Fsck(std::move(device), options);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Consistency check failed for blobfs" << zx_status_get_string(status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

}  // namespace blobfs
