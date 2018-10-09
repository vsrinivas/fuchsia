// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fzl/mapped-vmo.h>
#include <lib/cksum.h>
#include <pretty/hexdump.h>
#include <zircon/assert.h>
#include <zircon/device/nand.h>
#include <zircon/device/nand-broker.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zxcpp/new.h>

#include "aml.h"

namespace {

constexpr char kUsageMessage[] = R"""(
Low level access tool for a NAND device.
WARNING: This tool may overwrite the NAND device.

./nand-util --device /dev/sys/platform/05:00:d/aml-raw_nand/nand/broker --info

Note that to use this tool the driver binding rules have to be adjusted so that
the broker driver is loaded for the desired NAND device.

Options:
  --device (-d) path : Specifies the broker device to use.
  --info (-i) : Show basic NAND information.
  --bbt (-t) : Display bad block info.
  --read (-r) --absolute xxx : Read the page number xxx.
  --erase (-e) --block xxx --count yyy: Erase yyy blocks starting at xxx.
  --check (-c) : Looks for read errors on the device.
  --save (-s) --block xxx --file path: Save the block xxx to path.
  --file (-f) path:  Path to use when saving data.
  --absolute (-a) xxx : Use an absolute page number.
  --page (-p) xxx : Use the xxx page number (from within a block).
  --block (-b) xxx : Use the xxx block number (0-based).
  --count (-n) xxx : Limit the operation to xxx blocks.
                     Only supported with --check, --erase and --save.
  --live-dangerously (-y) : Don't prompt for confirmation.
)""";

// Configuration info (what to do).
struct Config {
    const char* path;
    const char* file;
    uint32_t page_num;
    uint32_t block_num;
    uint32_t abs_page;
    uint32_t count;
    int actions;
    bool info;
    bool bbt;
    bool read;
    bool erase;
    bool read_check;
    bool save;
    bool skip_prompt;
};

// Broker device wrapper.
class NandBroker {
  public:
    explicit NandBroker(const char* path) : device_(open(path, O_RDWR)) {}
    ~NandBroker() {}

    // Returns true on success.
    bool Initialize();

    // Returns a file descriptor for the device.
    int get() const { return device_.get(); }

    // The internal buffer can access a block at a time.
    const char* data() const { return reinterpret_cast<char*>(vmo_->GetData()); }
    const char* oob() const { return data() + info_.page_size * info_.pages_per_block; }

    const nand_info_t& Info() const { return info_; }

    // The operations to perform:
    bool Query();
    void ShowInfo() const;
    bool ReadPages(uint32_t first_page, uint32_t count) const;
    bool DumpPage(uint32_t page) const;
    bool EraseBlock(uint32_t block) const;

  private:
    fbl::unique_fd device_;
    nand_info_t info_ = {};
    fbl::unique_ptr<fzl::MappedVmo> vmo_;
};

bool NandBroker::Initialize()  {
    if (!Query()) {
        printf("Failed to open or query the device\n");
        return false;
    }
    const uint32_t size = (info_.page_size + info_.oob_size) * info_.pages_per_block;
    if (fzl::MappedVmo::Create(size, nullptr, &vmo_) != ZX_OK) {
        printf("Failed to allocate VMO\n");
        return false;
    }
    return true;
}

bool NandBroker::Query() {
    if (!device_) {
        return false;
    }

    return ioctl_nand_broker_get_info(device_.get(), &info_) == sizeof(info_);
}

void NandBroker::ShowInfo() const {
    printf("Page size: %d\nPages per block: %d\nTotal Blocks: %d\nOOB size: %d\nECC bits: %d\n"
           "Nand class: %d\n", info_.page_size, info_.pages_per_block, info_.num_blocks,
           info_.oob_size, info_.ecc_bits, info_.nand_class);
}

bool NandBroker::ReadPages(uint32_t first_page, uint32_t count) const {
    ZX_DEBUG_ASSERT(count <= info_.pages_per_block);
    nand_broker_request_t request = {};
    nand_broker_response_t response = {};

    request.length = count;
    request.offset_nand = first_page;
    request.offset_oob_vmo = info_.pages_per_block;  // OOB is at the end of the VMO.
    request.data_vmo = true;
    request.oob_vmo = true;

    if (zx_handle_duplicate(vmo_->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &request.vmo) != ZX_OK) {
        printf("Failed to duplicate VMO\n");
        return false;
    }

    if (ioctl_nand_broker_read(get(), &request, &response) != sizeof(response)) {
        printf("Failed to issue command to driver\n");
        return false;
    }

    if (response.status != ZX_OK) {
        printf("Read to %d pages starting at %d failed with %s\n", count, first_page,
               zx_status_get_string(response.status));
        return false;
    }

    if (response.corrected_bit_flips > info_.ecc_bits) {
        printf("Read to %d pages starting at %d unable to correct all bit flips\n", count,
               first_page);
    } else if (response.corrected_bit_flips) {
        // If the nand protocol is modified to provide more info, we could display something
        // like average bit flips.
        printf("Read to %d pages starting at %d corrected %d errors\n", count, first_page,
               response.corrected_bit_flips);
    }

    return true;
}

bool NandBroker::DumpPage(uint32_t page) const {
    if (!ReadPages(page, 1)) {
        return false;
    }
    ZX_DEBUG_ASSERT(info_.page_size % 16 == 0);

    uint32_t address = page * info_.page_size;
    hexdump8_ex(data(), 16, address);
    int skip = 0;

    for (uint32_t line = 16; line < info_.page_size; line += 16) {
        if (memcmp(data() + line, data() + line - 16, 16) == 0) {
            skip++;
            if (skip < 50) {
                printf(".");
            }
            continue;
        }
        if (skip) {
            printf("\n");
            skip = 0;
        }
        hexdump8_ex(data() + line, 16, address + line);
    }

    if (skip) {
        printf("\n");
    }

    printf("OOB:\n");
    hexdump8_ex(oob(), info_.oob_size, address + info_.page_size);
    return true;
}

bool NandBroker::EraseBlock(uint32_t block) const {
    nand_broker_request_t request = {};
    nand_broker_response_t response = {};

    request.length = 1;
    request.offset_nand = block;

    if (ioctl_nand_broker_erase(get(), &request, &response) != sizeof(response)) {
        printf("Failed to issue erase command for block %d\n", block);
        return false;
    }

    if (response.status != ZX_OK) {
        printf("Erase block %d failed with %s\n", block, zx_status_get_string(response.status));
        return false;
    }

    return true;
}

bool GetOptions(int argc, char** argv, Config* config) {
    while (true) {
        struct option options[] = {
            {"device", required_argument, nullptr, 'd'},
            {"info", no_argument, nullptr, 'i'},
            {"bbt", no_argument, nullptr, 't'},
            {"read", no_argument, nullptr, 'r'},
            {"erase", no_argument, nullptr, 'e'},
            {"check", no_argument, nullptr, 'c'},
            {"save", no_argument, nullptr, 's'},
            {"file", required_argument, nullptr, 'f'},
            {"page", required_argument, nullptr, 'p'},
            {"block", required_argument, nullptr, 'b'},
            {"absolute", required_argument, nullptr, 'a'},
            {"count", required_argument, nullptr, 'n'},
            {"live-dangerously", no_argument, nullptr, 'y'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "d:irtecsf:p:b:a:n:h", options, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'd':
            config->path = optarg;
            break;
        case 'i':
            config->info = true;
            break;
        case 't':
            config->bbt = true;
            config->actions++;
            break;
        case 'r':
            config->read = true;
            config->actions++;
            break;
        case 'e':
            config->erase = true;
            config->actions++;
            break;
        case 'c':
            config->read_check = true;
            config->actions++;
            break;
        case 's':
            config->save = true;
            config->actions++;
            break;
        case 'f':
            config->file = optarg;
            break;
        case 'p':
            config->page_num = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'b':
            config->block_num = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'a':
            config->abs_page = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'n':
            config->count = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'y':
            config->skip_prompt = true;
            break;
        case 'h':
            printf("%s\n", kUsageMessage);
            return 0;
        }
    }
    return argc == optind;
}

bool ValidateOptions(const Config& config) {
    if (!config.path) {
        printf("Device needed\n");
        printf("%s\n", kUsageMessage);
        return false;
    }

    if (config.actions > 1) {
        printf("Only one action allowed\n");
        return false;
    }

    if (config.abs_page && config.page_num) {
        printf("Provide either a block + page or an absolute page number\n");
        return false;
    }

    if ((config.erase || config.save) && (config.page_num || config.abs_page)) {
        printf("The operation works with blocks, not pages\n");
        return false;
    }

    if (!config.info && !config.actions) {
        printf("Nothing to do\n");
        return false;
    }

    if (config.save && !config.file) {
        printf("Save requires a file\n");\
        return false;
    }

    if (config.count && (!config.read_check && !config.save && !config.erase)) {
        printf("Count not supported for this operation\n");
        return false;
    }
    return true;
}

bool ValidateOptionsWithNand(const NandBroker& nand, const Config& config) {
    if (config.page_num >= nand.Info().pages_per_block) {
        printf("Page not within a block:\n");
        return false;
    }

    if (config.block_num >= nand.Info().num_blocks) {
        printf("Block not within device:\n");
        return false;
    }

    if (config.abs_page >= nand.Info().num_blocks * nand.Info().pages_per_block) {
        printf("Page not within device:\n");
        return false;
    }

    if (config.erase && nand.Info().nand_class == NAND_CLASS_PARTMAP && config.block_num < 24) {
        printf("Erasing the restricted area is not a good idea, sorry\n");
        return false;
    }

    return true;
}

bool FindBadBlocks(const NandBroker& nand) {
    if (!nand.ReadPages(0, 1)) {
        return false;
    }

    uint32_t first_block;
    uint32_t num_blocks;
    GetBbtLocation(nand.data(), &first_block, &num_blocks);
    bool found = false;
    for (uint32_t block = 0; block < num_blocks; block++) {
        uint32_t start = (first_block + block) * nand.Info().pages_per_block;
        if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
            return false;
        }
        if (!DumpBbt(nand.data(), nand.oob(), nand.Info())) {
            break;
        }
        found = true;
    }
    if (!found) {
        printf("Unable to find any table\n");
    }
    return found;
}

// Verifies that reads always return the same data.
bool ReadCheck(const NandBroker& nand, uint32_t first_block, uint32_t count) {
    constexpr int kNumReads = 10;
    uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
    size_t size = (nand.Info().page_size + nand.Info().oob_size) * nand.Info().pages_per_block;
    for (uint32_t block = first_block; block < last_block; block++) {
        uint32_t first_crc;
        for (int i = 0; i < kNumReads; i++) {
            const uint32_t start = block * nand.Info().pages_per_block;
            if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
                printf("\nRead failed for block %u\n", block);
                return false;
            }
            const uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(nand.data()), size);
            if (!i) {
                first_crc = crc;
            } else if (first_crc != crc) {
                printf("\nMismatched reads on block %u\n", block);
                return false;
            }
        }
        printf("Block %u\r", block);
    }
    printf("\ndone\n");
    return true;
}

// Saves data from a nand device to a file at |path|.
bool Save(const NandBroker& nand, uint32_t first_block, uint32_t count, const char* path) {
    fbl::unique_fd out(open(path, O_WRONLY | O_CREAT | O_TRUNC));
    if (!out) {
        printf("Unable to open destination\n");
        return false;
    }

    // Attempt to save everything by default.
    count = count ? count : nand.Info().num_blocks;


    uint32_t block_oob_size = nand.Info().pages_per_block * nand.Info().oob_size;
    uint32_t oob_size = count * block_oob_size;
    fbl::unique_ptr<uint8_t[]> oob(new uint8_t[oob_size]);

    uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
    size_t data_size = nand.Info().page_size * nand.Info().pages_per_block;
    for (uint32_t block = first_block; block < last_block; block++) {
        const uint32_t start = block * nand.Info().pages_per_block;
        if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
            printf("\nRead failed for block %u\n", block);
            return false;
        }
        if (write(out.get(), nand.data(), data_size) != static_cast<ssize_t>(data_size)) {
            printf("\nFailed to write data for block %u\n", block);
            return false;
        }
        memcpy(oob.get() + block_oob_size * block, nand.oob(), block_oob_size);
        printf("Block %u\r", block);
    }

    if (write(out.get(), oob.get(), oob_size) != static_cast<ssize_t>(oob_size)) {
        printf("\nFailed to write oob\n");
        return false;
    }

    printf("\ndone\n");
    return true;
}

// Erases blocks from a nand device.
bool Erase(const NandBroker& nand, uint32_t first_block, uint32_t count) {
    uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
    for (uint32_t block = first_block; block < last_block; block++) {
        // Ignore failures, move on to the next one.
        nand.EraseBlock(block);
    }
    printf("\ndone\n");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Config config = {};
    if (!GetOptions(argc, argv, &config)) {
        printf("%s\n", kUsageMessage);
        return -1;
    }

    if (!ValidateOptions(config)) {
        return -1;
    }

    NandBroker nand(config.path);
    if (!nand.Initialize()) {
        printf("Unable to open the nand device\n");
        return -1;
    }

    if (config.info) {
        nand.ShowInfo();
        if (!nand.ReadPages(0, 1)) {
            return -1;
        }
        DumpPage0(nand.data());
    }

    if (config.bbt) {
        return FindBadBlocks(nand) ? 0 : -1;
    }

    if (!ValidateOptionsWithNand(nand, config)) {
        nand.ShowInfo();
        return -1;
    }

    if (config.read) {
        if (!config.abs_page) {
            config.abs_page = config.block_num * nand.Info().pages_per_block + config.page_num;
        }
        printf("To read page %d\n", config.abs_page);
        return nand.DumpPage(config.abs_page) ? 0 : -1;
    }

    if (config.erase) {
        // Erase a single block by default.
        config.count = config.count ? config.count : 1;
        if (!config.skip_prompt) {
            printf("About to erase %d block(s) starting at block %d. Press y to confirm\n",
                   config.count, config.block_num);
            if (getchar() != 'y') {
                return -1;
            }
        }
        return Erase(nand, config.block_num, config.count) ? 0 : -1;
    }

    if (config.read_check) {
        printf("Checking blocks...\n");
        return ReadCheck(nand, config.block_num, config.count) ? 0 : -1;
    }

    if (config.save) {
        printf("Saving blocks...\n");
        return Save(nand, config.block_num, config.count, config.file) ? 0 : -1;
    }

    return 0;
}
