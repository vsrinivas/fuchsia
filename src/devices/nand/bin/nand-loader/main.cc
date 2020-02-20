// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <ramdevice-client/ramnand.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Creates a ram-nand device using an optional saved image file.

To use an image file:
nand-loader image_file

To create an empty 32 MB ram-nand device:
nand-loader --num-blocks 128

Options:
  --page-size (-p) xxx : NAND page size. Default: 4096.
  --oob-size (-o) xxx : NAND OOB size. Default: 8.
  --block-size (-b) xxx : NAND pages per block. Default: 64.
  --num-blocks (-n) xxx : number of NAND blocks. Not valid with an image file.
)""";

struct Config {
  const char* path = nullptr;
  uint32_t page_size = 4096;
  uint32_t oob_size = 8;
  uint32_t block_size = 64;
  uint32_t num_blocks = 0;
};

bool GetOptions(int argc, char** argv, Config* config) {
  while (true) {
    struct option options[] = {
        {"page-size", required_argument, nullptr, 'p'},
        {"oob-size", required_argument, nullptr, 'o'},
        {"block-size", required_argument, nullptr, 'b'},
        {"num-blocks", required_argument, nullptr, 'n'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "p:b:n:h", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'p':
        config->page_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'o':
        config->oob_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'b':
        config->block_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'n':
        config->num_blocks = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'h':
      default:
        return false;
    }
  }
  if (argc == optind + 1) {
    config->path = argv[optind];
  }
  return true;
}

bool ValidateOptions(const Config& config) {
  if (!config.path && !config.num_blocks) {
    printf("Image file needed\n");
    printf("%s\n", kUsageMessage);
    return false;
  }

  if (config.path && config.num_blocks) {
    printf("Cannot specify size with an image file\n");
    return false;
  }

  if (config.page_size % 2048 != 0) {
    printf("Page size not multiple of 2048\n");
    return false;
  }

  return true;
}

fuchsia_hardware_nand_Info GetNandInfo(const Config& config) {
  fuchsia_hardware_nand_Info info = {};
  info.page_size = config.page_size;
  info.pages_per_block = config.block_size;
  info.num_blocks = config.num_blocks;
  info.ecc_bits = 8;
  info.oob_size = config.oob_size;
  info.nand_class = fuchsia_hardware_nand_Class_FTL;
  return info;
}

// Sets the vmo and nand size from the contents of the input file.
bool FinishDeviceConfig(const char* path, fuchsia_hardware_nand_RamNandInfo* device_config) {
  if (!path) {
    return true;
  }

  fbl::unique_fd in(open(path, O_RDONLY));
  if (!in) {
    printf("Unable to open image file\n");
    return false;
  }

  off_t in_size = lseek(in.get(), 0, SEEK_END);
  if (in_size < 0) {
    printf("Unable to get file length\n");
    return false;
  }
  fuchsia_hardware_nand_Info& info = device_config->nand_info;

  uint32_t block_size = info.pages_per_block * (info.oob_size + info.page_size);
  if (in_size % block_size != 0) {
    printf("Unexpected file length for NAND parameters\n");
    return false;
  }
  info.num_blocks = static_cast<uint32_t>(in_size / block_size);

  fzl::OwnedVmoMapper mapper;
  zx_status_t status = mapper.CreateAndMap(in_size, "nand-loader");
  if (status != ZX_OK) {
    printf("Unable to create VMO\n");
    return false;
  }

  if (pread(in.get(), mapper.start(), in_size, 0) != static_cast<ssize_t>(in_size)) {
    printf("Unable to read data\n");
    return false;
  }

  zx::vmo dup;
  status = mapper.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    printf("Unable to duplicate VMO handle\n");
    return false;
  }
  device_config->vmo = dup.release();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!GetOptions(argc, argv, &config)) {
    printf("%s\n", kUsageMessage);
    return -1;
  }

  if (!ValidateOptions(config)) {
    return -1;
  }

  fuchsia_hardware_nand_RamNandInfo ram_nand_config = {};
  ram_nand_config.nand_info = GetNandInfo(config);
  if (!FinishDeviceConfig(config.path, &ram_nand_config)) {
    return -1;
  }

  std::optional<ramdevice_client::RamNand> ram_nand;
  if (ramdevice_client::RamNand::Create(&ram_nand_config, &ram_nand) != ZX_OK) {
    printf("Unable to load device\n");
    return -1;
  }
  printf("Device loaded: %s\n", ram_nand->path());

  // Purposefully prevent automatic removal of ram_nand in destructor.
  ram_nand->NoUnbind();
  return 0;
}
