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
#include <fs-management/ram-nand.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace {

constexpr char kUsageMessage[] = R"""(
Creates a ram-nand device using a saved image file.

nand-loader image_file

Options:
  --page-size (-p) xxx : NAND page size. Default: 4096.
  --block-size (-b) xxx : NAND pages per block. Default: 64.
)""";

struct Config {
    const char* path;
    uint32_t page_size;
    uint32_t block_size;
};

bool GetOptions(int argc, char** argv, Config* config) {
    while (true) {
        struct option options[] = {
            {"page-size", required_argument, nullptr, 'p'},
            {"block-size", required_argument, nullptr, 'b'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "p:b:h", options, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'p':
            config->page_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'b':
            config->block_size = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'h':
            return false;
        }
    }
    if (argc == optind + 1) {
        config->path = argv[optind];
        return true;
    }
    return false;
}

bool ValidateOptions(const Config& config) {
    if (!config.path) {
        printf("Image file needed\n");
        printf("%s\n", kUsageMessage);
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
    info.ecc_bits = 8;
    info.oob_size = 8;
    info.nand_class = fuchsia_hardware_nand_Class_FTL;
    return info;
};

// Sets the vmo and nand size from the contents of the input file.
bool FinishDeviceConfig(const char* path, fuchsia_hardware_nand_RamNandInfo* device_config) {
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
    Config config = {nullptr, 4096, 64};
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

    std::optional<fs_mgmt::RamNand> ram_nand;
    if (fs_mgmt::RamNand::Create(&ram_nand_config, &ram_nand) != ZX_OK) {
        printf("Unable to load device\n");
        return -1;
    }
    printf("Device loaded: %s\n", ram_nand->path());

    // Purposefully prevent automatic removal of ram_nand in destructor.
    ram_nand->NoUnbind();
    return 0;
}
