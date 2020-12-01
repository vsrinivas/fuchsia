// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <optional>
#include <utility>

#include <blobfs/blob-layout.h>
#include <blobfs/cache-policy.h>
#include <blobfs/compression-settings.h>
#include <blobfs/fsck.h>
#include <blobfs/mkfs.h>
#include <blobfs/mount.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs/vfs.h>

namespace {

using block_client::BlockDevice;
using block_client::RemoteBlockDevice;

// Parsed command line options for the different commands.
struct Options {
  blobfs::MountOptions mount_options;
  blobfs::FilesystemOptions mkfs_options;
};

zx::resource AttemptToGetVmexResource() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::resource();
  }
  status = fdio_service_connect("/svc_blobfs/fuchsia.kernel.VmexResource", remote.release());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to connect to fuchsia.kernel.VmexResource: " << status;
    return zx::resource();
  }

  auto client = llcpp::fuchsia::kernel::VmexResource::SyncClient{std::move(local)};
  auto result = client.Get();
  if (!result.ok()) {
    FX_LOGS(WARNING) << "fuchsia.kernel.VmexResource.Get() failed: " << result.status();
    return zx::resource();
  }
  return std::move(result->vmex_resource);
}

zx_status_t Mount(std::unique_ptr<BlockDevice> device, const Options& options) {
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  // TODO(fxbug.dev/34531): this currently supports both the old (data root only) and the new
  // (outgoing directory) behaviors. once all clients are moved over to using the new behavior,
  // delete the old one.
  zx::channel root_server = zx::channel(zx_take_startup_handle(FS_HANDLE_ROOT_ID));
  zx::channel diagnostics_dir = zx::channel(zx_take_startup_handle(FS_HANDLE_DIAGNOSTICS_DIR));

  if (outgoing_server.is_valid() && root_server.is_valid()) {
    FX_LOGS(ERROR) << "both PA_DIRECTORY_REQUEST and FS_HANDLE_ROOT_ID provided - need one or the "
                      "other.";
    return ZX_ERR_BAD_STATE;
  }

  zx::channel export_root;
  blobfs::ServeLayout layout;
  if (outgoing_server.is_valid()) {
    export_root = std::move(outgoing_server);
    layout = blobfs::ServeLayout::kExportDirectory;
  } else if (root_server.is_valid()) {
    export_root = std::move(root_server);
    layout = blobfs::ServeLayout::kDataRootOnly;
  } else {
    // neither provided? or we can't access them for some reason.
    FX_LOGS(ERROR) << "could not get startup handle to serve on";
    return ZX_ERR_BAD_STATE;
  }

  // Try and get a ZX_RSRC_SYSTEM_BASE_VMEX resource if the fuchsia.kernel.VmexResource service is
  // available, which will only be the case if this is launched by fshost. This is non-fatal because
  // blobfs can still otherwise work but will not support executable blobs.
  zx::resource vmex = AttemptToGetVmexResource();
  if (!vmex.is_valid()) {
    FX_LOGS(WARNING) << "VMEX resource unavailable, executable blobs are unsupported";
  }

  return blobfs::Mount(std::move(device), options.mount_options, std::move(export_root), layout,
                       std::move(vmex), std::move(diagnostics_dir));
}

zx_status_t Mkfs(std::unique_ptr<BlockDevice> device, const Options& options) {
  return blobfs::FormatFilesystem(device.get(), options.mkfs_options);
}

zx_status_t Fsck(std::unique_ptr<BlockDevice> device, const Options& options) {
  return blobfs::Fsck(std::move(device), options.mount_options);
}

typedef zx_status_t (*CommandFunction)(std::unique_ptr<BlockDevice> device, const Options& options);

const struct {
  const char* name;
  CommandFunction func;
  const char* help;
} kCmds[] = {
    {"create", Mkfs, "initialize filesystem"},     {"mkfs", Mkfs, "initialize filesystem"},
    {"check", Fsck, "check filesystem integrity"}, {"fsck", Fsck, "check filesystem integrity"},
    {"mount", Mount, "mount filesystem"},
};

std::optional<blobfs::CompressionAlgorithm> ParseAlgorithm(const char* str) {
  if (!strcmp(str, "UNCOMPRESSED")) {
    return blobfs::CompressionAlgorithm::UNCOMPRESSED;
  } else if (!strcmp(str, "ZSTD")) {
    return blobfs::CompressionAlgorithm::ZSTD;
  } else if (!strcmp(str, "ZSTD_SEEKABLE")) {
    return blobfs::CompressionAlgorithm::ZSTD_SEEKABLE;
  } else if (!strcmp(str, "ZSTD_CHUNKED")) {
    return blobfs::CompressionAlgorithm::CHUNKED;
  }
  return std::nullopt;
}

std::optional<blobfs::CachePolicy> ParseEvictionPolicy(const char* str) {
  if (!strcmp(str, "NEVER_EVICT")) {
    return blobfs::CachePolicy::NeverEvict;
  } else if (!strcmp(str, "EVICT_IMMEDIATELY")) {
    return blobfs::CachePolicy::EvictImmediately;
  }
  return std::nullopt;
}

std::optional<int> ParseInt(const char* str) {
  char* pend;
  long ret = strtol(str, &pend, 10);
  if (*pend != '\0') {
    return std::nullopt;
  } else if (ret < std::numeric_limits<int>::min() || ret > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(ret);
}

int usage() {
  fprintf(
      stderr,
      "usage: blobfs [ <options>* ] <command> [ <arg>* ]\n"
      "\n"
      "options: -v|--verbose   Additional debug logging\n"
      "         -r|--readonly              Mount filesystem read-only\n"
      "         -m|--metrics               Collect filesystem metrics\n"
      "         -c|--compression [alg]     compression algorithm to apply to newly stored blobs.\n"
      "                                    Does not affect any blobs already stored on-disk.\n"
      "                                    'alg' can be one of ZSTD, ZSTD_SEEKABLE, ZSTD_CHUNKED,\n"
      "                                    or UNCOMPRESSED.\n"
      "         -l|--compression_level n   Aggressiveness of compression to apply to newly stored\n"
      "                                    blobs. Only used if -c is one of ZSTD*, in which case\n"
      "                                    the level is the zstd compression level.\n"
      "         -e|--eviction_policy |pol| Policy for when to evict pager-backed blobs with no\n"
      "                                    handles. |pol| can be one of NEVER_EVICT or\n"
      "                                    EVICT_IMMEDIATELY.\n"
      "         -b|--blob_layout_format [padded|compact]\n"
      "                                    The format blobfs should use to store blobs.  Only\n"
      "                                    valid for mkfs.\n"
      "         -s|--sandbox_decompression Run blob decompression in a sandboxed component.\n"
      "         -h|--help                  Display this message\n"
      "\n"
      "On Fuchsia, blobfs takes the block device argument by handle.\n"
      "This can make 'blobfs' commands hard to invoke from command line.\n"
      "Try using the [mkfs,fsck,mount,umount] commands instead\n"
      "\n");

  for (unsigned n = 0; n < (sizeof(kCmds) / sizeof(kCmds[0])); n++) {
    fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", kCmds[n].name, kCmds[n].help);
  }
  fprintf(stderr, "\n");
  return ZX_ERR_INVALID_ARGS;
}

zx::status<Options> ProcessArgs(int argc, char** argv, CommandFunction* func) {
  Options options{};
  while (1) {
    static struct option opts[] = {

        {"verbose", no_argument, nullptr, 'v'},
        {"readonly", no_argument, nullptr, 'r'},
        {"metrics", no_argument, nullptr, 'm'},
        {"pager", no_argument, nullptr, 'p'},
        {"compression", required_argument, nullptr, 'c'},
        {"compression_level", required_argument, nullptr, 'l'},
        {"eviction_policy", required_argument, nullptr, 'e'},
        {"blob_layout_format", required_argument, nullptr, 'b'},
        {"sandbox_decompression", no_argument, nullptr, 's'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "vrmc:l:e:h", opts, &opt_index);

    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options.mount_options.writability = blobfs::Writability::ReadOnlyFilesystem;
        break;
      case 'm':
        options.mount_options.metrics = true;
        break;
      case 'c': {
        std::optional<blobfs::CompressionAlgorithm> algorithm = ParseAlgorithm(optarg);
        if (!algorithm) {
          fprintf(stderr, "Invalid compression algorithm: %s\n", optarg);
          return zx::error(usage());
        }
        options.mount_options.compression_settings.compression_algorithm = *algorithm;
        break;
      }
      case 'l': {
        std::optional<int> level = ParseInt(optarg);
        if (!level || level < 0) {
          fprintf(stderr, "Invalid argument for --compression_level: %s\n", optarg);
          return zx::error(usage());
        }
        options.mount_options.compression_settings.compression_level = level;
        break;
      }
      case 'e': {
        std::optional<blobfs::CachePolicy> policy = ParseEvictionPolicy(optarg);
        if (!policy) {
          fprintf(stderr, "Invalid eviction policy: %s\n", optarg);
          return zx::error(usage());
        }
        options.mount_options.pager_backed_cache_policy = policy;
        break;
      }
      case 'v':
        options.mount_options.verbose = true;
        break;
      case 'b': {
        auto format = blobfs::ParseBlobLayoutFormatCommandLineArg(optarg);
        if (format.is_error()) {
          fprintf(stderr, "Invalid blob layout format: %s\n", optarg);
          return zx::error(usage());
        }
        options.mkfs_options.blob_layout_format = format.value();
        break;
      }
      case 's': {
        options.mount_options.sandbox_decompression = true;
        break;
      }
      case 'h':
      default:
        return zx::error(usage());
    }
  }

  if (!options.mount_options.compression_settings.IsValid()) {
    fprintf(stderr, "Invalid compression settings.\n");
    return zx::error(usage());
  }

  argc -= optind;
  argv += optind;

  if (argc < 1) {
    return zx::error(usage());
  }
  const char* command = argv[0];

  // Validate command
  for (unsigned i = 0; i < sizeof(kCmds) / sizeof(kCmds[0]); i++) {
    if (!strcmp(command, kCmds[i].name)) {
      *func = kCmds[i].func;
    }
  }

  if (*func == nullptr) {
    fprintf(stderr, "Unknown command: %s\n", command);
    return zx::error(usage());
  }

  return zx::ok(options);
}
}  // namespace

int main(int argc, char** argv) {
  CommandFunction func = nullptr;
  auto options_or = ProcessArgs(argc, argv, &func);
  if (options_or.is_error()) {
    return EXIT_FAILURE;
  }
  const Options& options = options_or.value();

  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FX_LOGS(ERROR) << "Could not access startup handle to block device";
    return EXIT_FAILURE;
  }

  fbl::unique_fd svc_fd(open("/svc", O_RDONLY));
  if (!svc_fd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open svc from incoming namespace";
    return EXIT_FAILURE;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  zx_status_t status = RemoteBlockDevice::Create(std::move(block_connection), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    return EXIT_FAILURE;
  }
  status = func(std::move(device), options);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
