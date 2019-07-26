// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/directory.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include <cstdio>
#include <utility>

#include "payload-streamer.h"

#define ERROR(fmt, ...) fprintf(stderr, "disk-pave:[%s] " fmt, __FUNCTION__, ##__VA_ARGS__);

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
  ERROR("Options:\n");
  ERROR("  --file <file>: Read from FILE instead of stdin\n");
  ERROR("  --force: Install partition even if inappropriate for the device\n");
  ERROR("  --path <path>: Install DATA file to path\n");
}

// Refer to //zircon/system/fidl/fuchsia.paver/paver-fidl for a list of what
// these commands translate to.
enum class Command {
  kWipe,
  kAsset,
  kBootloader,
  kDataFile,
  kFvm,
};

struct Flags {
  Command cmd;
  ::llcpp::fuchsia::paver::Configuration configuration;
  ::llcpp::fuchsia::paver::Asset asset;
  fbl::unique_fd payload_fd;
  char* path = nullptr;
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
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::A;
    flags->asset = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (!strcmp(argv[0], "install-zircona")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::A;
    flags->asset = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (!strcmp(argv[0], "install-zirconb")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::B;
    flags->asset = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (!strcmp(argv[0], "install-zirconr")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::RECOVERY;
    flags->asset = ::llcpp::fuchsia::paver::Asset::KERNEL;
  } else if (!strcmp(argv[0], "install-vbmetaa")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::A;
    flags->asset = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (!strcmp(argv[0], "install-vbmetab")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::B;
    flags->asset = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (!strcmp(argv[0], "install-vbmetar")) {
    flags->cmd = Command::kAsset;
    flags->configuration = ::llcpp::fuchsia::paver::Configuration::RECOVERY;
    flags->asset = ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA;
  } else if (!strcmp(argv[0], "install-data-file")) {
    flags->cmd = Command::kDataFile;
  } else if (!strcmp(argv[0], "install-fvm")) {
    flags->cmd = Command::kFvm;
  } else if (!strcmp(argv[0], "wipe")) {
    flags->cmd = Command::kWipe;
  } else {
    ERROR("Invalid command: %s\n", argv[0]);
    return false;
  }
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
    } else if (!strcmp(argv[0], "--path")) {
      SHIFT_ARGS;
      if (argc < 1) {
        ERROR("'--path' argument requires a path\n");
        return false;
      }
      flags->path = argv[0];
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

zx_status_t ReadFileToVmo(fbl::unique_fd payload_fd, ::llcpp::fuchsia::mem::Buffer* payload) {
  constexpr size_t VmoSize = fbl::round_up(1LU << 20, ZX_PAGE_SIZE);
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

  payload->size = vmo_offset;
  payload->vmo = mapper.Release();
  return ZX_OK;
}

zx_status_t RealMain(Flags flags) {
  zx::channel paver_remote, paver_svc;
  auto status = zx::channel::create(0, &paver_svc, &paver_remote);
  if (status != ZX_OK) {
    ERROR("Unable to create channels.\n");
    return status;
  }
  const auto path = fbl::StringPrintf("/svc/%s", ::llcpp::fuchsia::paver::Paver::Name);
  status = fdio_service_connect(path.c_str(), paver_remote.release());
  if (status != ZX_OK) {
    ERROR("Unable to open /svc/fuchsia.paver.Paver.\n");
    return status;
  }
  ::llcpp::fuchsia::paver::Paver::SyncClient paver_client(std::move(paver_svc));

  zx_status_t io_status = ZX_ERR_INTERNAL;
  switch (flags.cmd) {
    case Command::kFvm: {
      zx::channel client, server;
      status = zx::channel::create(0, &client, &server);
      if (status) {
        return status;
      }

      // Launch thread which implements interface.
      async::Loop loop(&kAsyncLoopConfigAttachToThread);
      disk_pave::PayloadStreamer streamer(std::move(server), std::move(flags.payload_fd));
      loop.StartThread("payload-stream");

      io_status = paver_client.WriteVolumes_Deprecated(std::move(client), &status);
      return io_status == ZX_OK ? status : io_status;
    }
    case Command::kWipe:
      io_status = paver_client.WipeVolumes_Deprecated(&status);
      return io_status == ZX_OK ? status : io_status;
    default:
      break;
  }

  ::llcpp::fuchsia::mem::Buffer payload;
  status = ReadFileToVmo(std::move(flags.payload_fd), &payload);
  if (status != ZX_OK) {
    return status;
  }

  switch (flags.cmd) {
    case Command::kDataFile: {
      if (flags.path == nullptr) {
        ERROR("install-data-file requires --path\n");
        PrintUsage();
        return ZX_ERR_INVALID_ARGS;
      }
      io_status = paver_client.WriteDataFile_Deprecated(
          fidl::StringView(strlen(flags.path), flags.path), std::move(payload), &status);
      break;
    }
    case Command::kBootloader:
      io_status = paver_client.WriteBootloader_Deprecated(std::move(payload), &status);
      break;
    case Command::kAsset:
      io_status = paver_client.WriteAsset_Deprecated(flags.configuration, flags.asset,
                                                     std::move(payload), &status);
      break;
    default:
      return ZX_ERR_INTERNAL;
  }

  return io_status == ZX_OK ? status : io_status;
}

}  // namespace

int main(int argc, char** argv) {
  Flags flags = {};
  if (!ParseFlags(argc, argv, &flags)) {
    PrintUsage();
    return -1;
  }
  return RealMain(std::move(flags)) ? 1 : 0;
}
