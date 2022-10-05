// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>

#include <cstdio>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "payload-streamer.h"

// Print a message to stderr, along with the program name and function name.
#define ERROR(fmt, ...) fprintf(stderr, "disk-pave:[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)

namespace {

void PrintUsage() {
  ERROR("install-disk-image <command> [options...]\n");
  ERROR("Commands:\n");
  ERROR("  install-bootloader : Install a BOOTLOADER partition to the device\n");
  ERROR("  install-zircona    : Install a ZIRCON-A partition to the device\n");
  ERROR("  install-zirconb    : Install a ZIRCON-B partition to the device\n");
  ERROR("  install-zirconr    : Install a ZIRCON-R partition to the device\n");
  ERROR("  install-vbmetaa    : Install a VBMETA-A partition to the device\n");
  ERROR("  install-vbmetab    : Install a VBMETA-B partition to the device\n");
  ERROR("  install-vbmetar    : Install a VBMETA-R partition to the device\n");
  ERROR("  install-fvm        : Install a sparse FVM to the device\n");
  ERROR("  install-data-file  : Install a file to DATA (--path required)\n");
  ERROR("  wipe               : Remove the FVM partition\n");
  ERROR("  init-partition-tables : Initialize block device with valid GPT and FVM\n");
  ERROR("  wipe-partition-tables : Remove all partitions for partition table\n");
  ERROR("Options:\n");
  ERROR("  --file <file>: Read from FILE instead of stdin\n");
  ERROR("  --force: Install partition even if inappropriate for the device\n");
  ERROR("  --path <path>: Install DATA file to path\n");
  ERROR(
      "  --block-device <path>: Block device to operate on. Only applies to wipe, "
      "init-partition-tables, and wipe-partition-tables\n");
}

// Refer to //sdk/fidl/fuchsia.paver/paver.fidl for a list of what
// these commands translate to.
enum class Command {
  kWipe,
  kWipePartitionTables,
  kInitPartitionTables,
  kAsset,
  kBootloader,
  kDataFile,
  kFvm,
};

struct Flags {
  Command cmd;
  const char* cmd_name = nullptr;
  fuchsia_paver::wire::Configuration configuration;
  fuchsia_paver::wire::Asset asset;
  fbl::unique_fd payload_fd;
  const char* path = nullptr;
  const char* block_device = nullptr;
};

bool ParseFlags(int argc, char** argv, Flags* flags) {
#define SHIFT_ARGS \
  do {             \
    argc--;        \
    argv++;        \
  } while (0)

  // Parse command.
  if (argc < 2) {
    ERROR("install-disk-image needs a command\n");
    return false;
  }
  SHIFT_ARGS;

  if (!strcmp(argv[0], "install-bootloader")) {
    flags->cmd = Command::kBootloader;
  } else if (!strcmp(argv[0], "install-efi")) {
    flags->cmd = Command::kBootloader;
  } else if (!strcmp(argv[0], "install-kernc")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kA;
    flags->asset = fuchsia_paver::wire::Asset::kKernel;
  } else if (!strcmp(argv[0], "install-zircona")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kA;
    flags->asset = fuchsia_paver::wire::Asset::kKernel;
  } else if (!strcmp(argv[0], "install-zirconb")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kB;
    flags->asset = fuchsia_paver::wire::Asset::kKernel;
  } else if (!strcmp(argv[0], "install-zirconr")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kRecovery;
    flags->asset = fuchsia_paver::wire::Asset::kKernel;
  } else if (!strcmp(argv[0], "install-vbmetaa")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kA;
    flags->asset = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
  } else if (!strcmp(argv[0], "install-vbmetab")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kB;
    flags->asset = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
  } else if (!strcmp(argv[0], "install-vbmetar")) {
    flags->cmd = Command::kAsset;
    flags->configuration = fuchsia_paver::wire::Configuration::kRecovery;
    flags->asset = fuchsia_paver::wire::Asset::kVerifiedBootMetadata;
  } else if (!strcmp(argv[0], "install-data-file")) {
    flags->cmd = Command::kDataFile;
  } else if (!strcmp(argv[0], "install-fvm")) {
    flags->cmd = Command::kFvm;
  } else if (!strcmp(argv[0], "wipe")) {
    flags->cmd = Command::kWipe;
  } else if (!strcmp(argv[0], "init-partition-tables")) {
    flags->cmd = Command::kInitPartitionTables;
  } else if (!strcmp(argv[0], "wipe-partition-tables")) {
    flags->cmd = Command::kWipePartitionTables;
  } else {
    ERROR("Invalid command: %s\n", argv[0]);
    return false;
  }
  flags->cmd_name = argv[0];
  SHIFT_ARGS;

  // Parse options.
  flags->payload_fd.reset(STDIN_FILENO);
  while (argc > 0) {
    if (!strcmp(argv[0], "--file")) {
      SHIFT_ARGS;
      if (argc < 1) {
        ERROR("'--file' argument requires a file\n");
        return false;
      }
      flags->payload_fd.reset(open(argv[0], O_RDONLY));
      if (!flags->payload_fd) {
        ERROR("Couldn't open supplied file\n");
        return false;
      }
      struct stat s;
      if (fstat(flags->payload_fd.get(), &s) == 0) {
        ERROR("Opening file \"%s\" of size: %llu\n", argv[0], s.st_size);
      } else {
        // This is purely informational. Don't return failure.
        ERROR("Failed to stat \"%s\"\n", argv[0]);
      }
    } else if (!strcmp(argv[0], "--path")) {
      SHIFT_ARGS;
      if (argc < 1) {
        ERROR("'--path' argument requires a path\n");
        return false;
      }
      flags->path = argv[0];
    } else if (!strcmp(argv[0], "--block-device")) {
      SHIFT_ARGS;
      if (argc < 1) {
        ERROR("'--block-device' argument requires a path\n");
        return false;
      }
      flags->block_device = argv[0];
    } else if (!strcmp(argv[0], "--force")) {
      ERROR("Deprecated option \"--force\".");
    } else {
      return false;
    }
    SHIFT_ARGS;
  }
  return true;
#undef SHIFT_ARGS
}

zx_status_t ReadFileToVmo(fbl::unique_fd payload_fd, fuchsia_mem::wire::Buffer* payload) {
  const size_t VmoSize = fbl::round_up(1LU << 20, zx_system_get_page_size());
  fzl::ResizeableVmoMapper mapper;
  zx_status_t status;
  if ((status = mapper.CreateAndMap(VmoSize, "partition-pave")) != ZX_OK) {
    ERROR("Failed to create stream VMO\n");
    return status;
  }

  ssize_t r;
  size_t vmo_offset = 0;
  while ((r = read(payload_fd.get(), &reinterpret_cast<uint8_t*>(mapper.start())[vmo_offset],
                   mapper.size() - vmo_offset)) > 0) {
    vmo_offset += r;
    if (mapper.size() - vmo_offset == 0) {
      // The buffer is full, let's grow the VMO.
      if ((status = mapper.Grow(mapper.size() << 1)) != ZX_OK) {
        ERROR("Failed to grow VMO\n");
        return status;
      }
    }
  }

  if (r < 0) {
    ERROR("Error reading partition data\n");
    return static_cast<zx_status_t>(r);
  }

  auto vmo = mapper.Release();
  status = vmo.set_prop_content_size(vmo_offset);
  if (status != ZX_OK) {
    return status;
  }

  payload->size = vmo_offset;
  payload->vmo = std::move(vmo);
  return ZX_OK;
}

// Protocol should be either |DataSink| or |DynamicDataSink|.
template <typename Protocol>
struct UseBlockDeviceError {
  zx_status_t error;
  fidl::ServerEnd<Protocol> unused_server;
};

// If the block device was opened successfully, tells the paver client to
// use that block device. Otherwise, hands back the unused data sink server-end
// together with a status.
template <typename Protocol>
fit::result<UseBlockDeviceError<Protocol>> UseBlockDevice(
    fidl::WireSyncClient<fuchsia_paver::Paver>& paver_client, const char* block_device_path,
    fidl::ServerEnd<Protocol> data_sink_remote) {
  static_assert(std::is_same_v<Protocol, fuchsia_paver::DataSink> ||
                std::is_same_v<Protocol, fuchsia_paver::DynamicDataSink>);

  auto block_device = component::Connect<fuchsia_hardware_block::Block>(block_device_path);
  if (block_device.is_ok()) {
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)paver_client->UseBlockDevice(
        std::move(*block_device),
        // Note: manually converting any DataSink protocol into a DynamicDataSink.
        fidl::ServerEnd<fuchsia_paver::DynamicDataSink>(data_sink_remote.TakeChannel()));
    return fit::ok();
  }

  ERROR("Unable to open block device: %s (%s)\n", block_device_path, block_device.status_string());
  PrintUsage();

  return fit::error(UseBlockDeviceError<Protocol>{
      block_device.status_value(),
      std::move(data_sink_remote),
  });
}

zx_status_t RealMain(Flags flags) {
  auto paver_svc = component::Connect<fuchsia_paver::Paver>();
  if (!paver_svc.is_ok()) {
    ERROR("Unable to open /svc/fuchsia.paver.Paver.\n");
    return paver_svc.error_value();
  }
  auto paver_client = fidl::WireSyncClient(std::move(*paver_svc));
  auto fshost_svc = component::Connect<fuchsia_fshost::Admin>();
  if (!fshost_svc.is_ok()) {
    ERROR("Unable to open /svc/fuchsia.fshost.Admin.\n");
    return fshost_svc.error_value();
  }
  auto fshost_client = fidl::WireSyncClient(std::move(*fshost_svc));

  switch (flags.cmd) {
    case Command::kFvm: {
      auto data_sink = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
      if (data_sink.is_error()) {
        ERROR("Unable to create channels.\n");
        return data_sink.status_value();
      }
      auto [data_sink_local, data_sink_remote] = std::move(*data_sink);
      // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
      (void)paver_client->FindDataSink(std::move(data_sink_remote));

      auto streamer_endpoints = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
      if (streamer_endpoints.is_error()) {
        return streamer_endpoints.status_value();
      }
      auto [client, server] = std::move(*streamer_endpoints);

      // Launch thread which implements interface.
      async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
      disk_pave::PayloadStreamer streamer(std::move(server), std::move(flags.payload_fd));
      loop.StartThread("payload-stream");

      auto result =
          fidl::WireSyncClient(std::move(data_sink_local))->WriteVolumes(std::move(client));
      zx_status_t status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Failed to write volumes: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    case Command::kWipe: {
      auto data_sink = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
      if (data_sink.is_error()) {
        ERROR("Unable to create channels.\n");
        return data_sink.status_value();
      }
      auto [data_sink_local, data_sink_remote] = std::move(*data_sink);

      // Try to use |block_device| path if provided.
      if (flags.block_device != nullptr) {
        auto result = UseBlockDevice(paver_client, flags.block_device, std::move(data_sink_remote));
        if (result.is_error()) {
          // Fallback to |FindDataSink|.
          data_sink_remote = std::move(result.error_value().unused_server);
        }
      }

      // If we do not have a valid block device, use |FindDataSink|.
      if (data_sink_remote) {
        // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
        (void)paver_client->FindDataSink(std::move(data_sink_remote));
      }

      auto result = fidl::WireSyncClient(std::move(data_sink_local))->WipeVolume();
      zx_status_t status;
      if (!result.ok()) {
        status = result.status();
      } else {
        status = !result->is_ok() ? ZX_OK : result->error_value();
      }
      if (status != ZX_OK) {
        ERROR("Failed to wipe block device: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    case Command::kInitPartitionTables: {
      if (flags.block_device == nullptr) {
        ERROR("init-partition-tables requires --block-device\n");
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
      }

      auto data_sink = fidl::CreateEndpoints<fuchsia_paver::DynamicDataSink>();
      if (data_sink.is_error()) {
        ERROR("Unable to create channels.\n");
        return data_sink.status_value();
      }
      auto [data_sink_local, data_sink_remote] = std::move(*data_sink);

      auto block_result =
          UseBlockDevice(paver_client, flags.block_device, std::move(data_sink_remote));
      if (block_result.is_error()) {
        return block_result.error_value().error;
      }

      auto result = fidl::WireSyncClient(std::move(data_sink_local))->InitializePartitionTables();
      zx_status_t status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Failed to initialize partition tables: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    case Command::kWipePartitionTables: {
      if (flags.block_device == nullptr) {
        ERROR("wipe-partition-tables requires --block-device\n");
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
      }

      auto data_sink = fidl::CreateEndpoints<fuchsia_paver::DynamicDataSink>();
      if (data_sink.is_error()) {
        ERROR("Unable to create channels.\n");
        return data_sink.status_value();
      }
      auto [data_sink_local, data_sink_remote] = std::move(*data_sink);

      auto block_result =
          UseBlockDevice(paver_client, flags.block_device, std::move(data_sink_remote));
      if (block_result.is_error()) {
        return block_result.error_value().error;
      }

      auto result = fidl::WireSyncClient(std::move(data_sink_local))->WipePartitionTables();
      zx_status_t status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Failed to wipe partition tables: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    default:
      break;
  }

  fuchsia_mem::wire::Buffer payload;
  zx_status_t status = ReadFileToVmo(std::move(flags.payload_fd), &payload);
  if (status != ZX_OK) {
    return status;
  }

  auto data_sink_endpoints = fidl::CreateEndpoints<fuchsia_paver::DataSink>();
  if (data_sink_endpoints.is_error()) {
    ERROR("Unable to create channels.\n");
    return data_sink_endpoints.status_value();
  }
  auto [data_sink_local, data_sink_remote] = std::move(*data_sink_endpoints);

  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)paver_client->FindDataSink(std::move(data_sink_remote));
  fidl::WireSyncClient data_sink{std::move(data_sink_local)};

  switch (flags.cmd) {
    case Command::kDataFile: {
      if (flags.path == nullptr) {
        ERROR("install-data-file requires --path\n");
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
      }
      auto result = fshost_client->WriteDataFile(fidl::StringView::FromExternal(flags.path),
                                                 std::move(payload.vmo));
      if (!result.ok()) {
        ERROR("install-data-file failed: %s\n", result.status_string());
        return result.status();
      }

      return ZX_OK;
    }
    case Command::kBootloader: {
      auto result = data_sink->WriteBootloader(std::move(payload));
      status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Installing bootloader partition failed: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    case Command::kAsset: {
      auto result = data_sink->WriteAsset(flags.configuration, flags.asset, std::move(payload));
      status = result.ok() ? result.value().status : result.status();
      if (status != ZX_OK) {
        ERROR("Writing asset failed: %s\n", zx_status_get_string(status));
        return status;
      }

      return ZX_OK;
    }
    default:
      return ZX_ERR_INTERNAL;
  }
}

}  // namespace

int main(int argc, char** argv) {
  Flags flags = {};
  if (!ParseFlags(argc, argv, &flags)) {
    PrintUsage();
    return -1;
  }
  const char* cmd_name = flags.cmd_name;

  zx_status_t status = RealMain(std::move(flags));
  if (status != ZX_OK) {
    return 1;
  }

  fprintf(stderr, "disk-pave: %s operation succeeded.\n", cmd_name);
  return 0;
}
