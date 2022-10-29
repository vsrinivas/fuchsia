// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <getopt.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <lib/zx/result.h>

#include <optional>
#include <utility>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/bin/blobfs/blobfs_component_config.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/cache_policy.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/mount.h"

namespace {

using block_client::RemoteBlockDevice;

// Parsed command line options for the different commands.
struct Options {
  blobfs::MountOptions mount_options;
  blobfs::FilesystemOptions mkfs_options;
};

zx::resource AttemptToGetVmexResource() {
  auto client_end_or = component::Connect<fuchsia_kernel::VmexResource>();
  if (client_end_or.is_error()) {
    FX_LOGS(WARNING) << "Failed to connect to fuchsia.kernel.VmexResource: "
                     << client_end_or.status_string();
    return zx::resource();
  }

  auto result = fidl::WireCall(*client_end_or)->Get();
  if (!result.ok()) {
    FX_LOGS(WARNING) << "fuchsia.kernel.VmexResource.Get() failed: " << result.error();
    return zx::resource();
  }

  return std::move(result.value().resource);
}

zx_status_t Mount(const Options& options) {
  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FX_LOGS(ERROR) << "Could not access startup handle to block device";
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  zx_status_t status = RemoteBlockDevice::Create(
      fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_connection)), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    return ZX_ERR_INTERNAL;
  }

  // Try and get a ZX_RSRC_SYSTEM_BASE_VMEX resource if the fuchsia.kernel.VmexResource service is
  // available, which will only be the case if this is launched by fshost. This is non-fatal because
  // blobfs can still otherwise work but will not support executable blobs.
  zx::resource vmex = AttemptToGetVmexResource();
  if (!vmex.is_valid()) {
    FX_LOGS(WARNING) << "VMEX resource unavailable, executable blobs are unsupported";
  }

  return blobfs::Mount(std::move(device), options.mount_options,
                       fidl::ServerEnd<fuchsia_io::Directory>(
                           zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST))),
                       std::move(vmex));
}

zx_status_t Mkfs(const Options& options) {
  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FX_LOGS(ERROR) << "Could not access startup handle to block device";
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  zx_status_t status = RemoteBlockDevice::Create(
      fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_connection)), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    return ZX_ERR_INTERNAL;
  }

  return blobfs::FormatFilesystem(device.get(), options.mkfs_options);
}

zx_status_t Fsck(const Options& options) {
  zx::channel block_connection = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
  if (!block_connection.is_valid()) {
    FX_LOGS(ERROR) << "Could not access startup handle to block device";
    return ZX_ERR_INTERNAL;
  }

  std::unique_ptr<RemoteBlockDevice> device;
  zx_status_t status = RemoteBlockDevice::Create(
      fidl::ClientEnd<fuchsia_hardware_block::Block>(std::move(block_connection)), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not initialize block device";
    return ZX_ERR_INTERNAL;
  }

  return blobfs::Fsck(std::move(device), options.mount_options);
}

zx_status_t StartComponent(const Options& _options) {
  FX_LOGS(INFO) << "starting blobfs component";

  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!outgoing_server.is_valid()) {
    FX_LOGS(ERROR) << "PA_DIRECTORY_REQUEST startup handle is required.";
    return ZX_ERR_INTERNAL;
  }
  fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir(std::move(outgoing_server));

  zx::channel lifecycle_channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
  if (!lifecycle_channel.is_valid()) {
    FX_LOGS(ERROR) << "PA_LIFECYCLE startup handle is required.";
    return ZX_ERR_INTERNAL;
  }
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request(
      std::move(lifecycle_channel));

  zx::resource vmex = AttemptToGetVmexResource();
  if (!vmex.is_valid()) {
    FX_LOGS(WARNING) << "VMEX resource unavailable, executable blobs are unsupported";
  }

  auto config = blobfs_component_config::Config::TakeFromStartupHandle();
  const blobfs::ComponentOptions options{
      .pager_threads = config.pager_threads(),
  };
  // blocks until blobfs exits
  zx::result status = blobfs::StartComponent(options, std::move(outgoing_dir),
                                             std::move(lifecycle_request), std::move(vmex));
  if (status.is_error()) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

using CommandFunction = zx_status_t (*)(const Options&);

const struct {
  const char* name;
  CommandFunction func;
  const char* help;
} kCmds[] = {{"create", Mkfs, "initialize filesystem"},
             {"mkfs", Mkfs, "initialize filesystem"},
             {"check", Fsck, "check filesystem integrity"},
             {"fsck", Fsck, "check filesystem integrity"},
             {"mount", Mount, "mount filesystem"},
             {"component", StartComponent, "start the blobfs component"}};

std::optional<blobfs::CompressionAlgorithm> ParseAlgorithm(const char* str) {
  if (!strcmp(str, "UNCOMPRESSED")) {
    return blobfs::CompressionAlgorithm::kUncompressed;
  }
  if (!strcmp(str, "ZSTD_CHUNKED")) {
    return blobfs::CompressionAlgorithm::kChunked;
  }
  return std::nullopt;
}

std::optional<blobfs::CachePolicy> ParseEvictionPolicy(const char* str) {
  if (!strcmp(str, "NEVER_EVICT")) {
    return blobfs::CachePolicy::NeverEvict;
  }
  if (!strcmp(str, "EVICT_IMMEDIATELY")) {
    return blobfs::CachePolicy::EvictImmediately;
  }
  return std::nullopt;
}

std::optional<int> ParseInt(const char* str) {
  char* pend;
  long ret = strtol(str, &pend, 10);
  if (*pend != '\0') {
    return std::nullopt;
  }
  if (ret < std::numeric_limits<int>::min() || ret > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(ret);
}

std::optional<int> ParseUint64(const char* str) {
  char* pend;
  unsigned long ret = strtoul(str, &pend, 10);
  if (*pend != '\0') {
    return std::nullopt;
  }
  if (ret > std::numeric_limits<uint64_t>::max()) {
    return std::nullopt;
  }
  return uint64_t{ret};
}

int usage() {
  fprintf(
      stderr,
      "usage: blobfs [ <options>* ] <command> [ <arg>* ]\n"
      "\n"
      "options: -v|--verbose   Additional debug logging\n"
      "         -r|--readonly              Mount filesystem read-only\n"
      "         -c|--compression [alg]     compression algorithm to apply to newly stored blobs.\n"
      "                                    Does not affect any blobs already stored on-disk.\n"
      "                                    'alg' can be one of ZSTD_CHUNKED or UNCOMPRESSED.\n"
      "         -l|--compression_level n   Aggressiveness of compression to apply to newly stored\n"
      "                                    blobs. Only used if -c is one of ZSTD*, in which case\n"
      "                                    the level is the zstd compression level.\n"
      "         -e|--eviction_policy |pol| Policy for when to evict pager-backed blobs with no\n"
      "                                    handles. |pol| can be one of NEVER_EVICT or\n"
      "                                    EVICT_IMMEDIATELY.\n"
      "         --deprecated_padded_format Turns on the deprecated format that uses more disk\n"
      "                                    space. Only valid for mkfs on Astro devices.\n"
      "         -i|--num_inodes n          The initial number of inodes to allocate space for.\n"
      "                                    Only valid for mkfs.\n"
      "         -s|--sandbox_decompression Run blob decompression in a sandboxed component.\n"
      "         -t|--paging_threads n      The number of threads to use in the pager\n"
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

zx::result<Options> ProcessArgs(int argc, char** argv, CommandFunction* func) {
  Options options{};

  // This option has no short flag, use int value beyond a char.
  constexpr int kDeprecatedPaddedFormat = 256;

  while (true) {
    static struct option opts[] = {
        {"verbose", no_argument, nullptr, 'v'},
        {"readonly", no_argument, nullptr, 'r'},
        {"pager", no_argument, nullptr, 'p'},
        {"compression", required_argument, nullptr, 'c'},
        {"compression_level", required_argument, nullptr, 'l'},
        {"eviction_policy", required_argument, nullptr, 'e'},
        {"deprecated_padded_format", no_argument, nullptr, kDeprecatedPaddedFormat},
        {"num_inodes", required_argument, nullptr, 'i'},
        {"sandbox_decompression", no_argument, nullptr, 's'},
        {"paging_threads", no_argument, nullptr, 't'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "vrmst:c:l:i:e:h", opts, &opt_index);

    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options.mount_options.writability = blobfs::Writability::ReadOnlyFilesystem;
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
      case 'i': {
        std::optional<uint64_t> num_inodes = ParseUint64(optarg);
        if (!num_inodes || *num_inodes == 0) {
          fprintf(stderr, "Invalid argument for --num_inodes: %s\n", optarg);
          return zx::error(usage());
        }
        options.mkfs_options.num_inodes = *num_inodes;
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
      case kDeprecatedPaddedFormat: {
        options.mkfs_options.blob_layout_format =
            blobfs::BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
        break;
      }
      case 's': {
        options.mount_options.sandbox_decompression = true;
        break;
      }
      case 't': {
        std::optional<int> num_threads = ParseInt(optarg);
        if (!num_threads || *num_threads <= 0) {
          fprintf(stderr, "Invalid argument for --paging_threads: %s\n", optarg);
          return zx::error(usage());
        }
        options.mount_options.paging_threads = *num_threads;
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
  for (const auto& cmd : kCmds) {
    if (!strcmp(command, cmd.name)) {
      *func = cmd.func;
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
  syslog::SetLogSettings({}, {"blobfs"});
  CommandFunction func = nullptr;
  auto options_or = ProcessArgs(argc, argv, &func);
  if (options_or.is_error()) {
    return EXIT_FAILURE;
  }
  const Options& options = options_or.value();

  zx_status_t status = func(options);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
