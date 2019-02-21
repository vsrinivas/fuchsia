// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <stdio.h>

#include "aml.h"
#include "commands.h"
#include "nand-broker.h"

namespace {

constexpr char kUsageMessage[] = R"""(
Low level access tool for a NAND device.
WARNING: This tool may overwrite the NAND device.

nand-util --device /dev/sys/platform/05:00:d/aml-raw_nand/nand/broker --info

Note that to use this tool either the driver binding rules have to be adjusted
so that the broker driver is loaded for the desired NAND device, or at least the
NAND device should not be bound to any other driver (like an FTL, skip-block or
or nandpart). This tool will attempt to load a broker driver if the device path
doesn't end with "/broker".

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

    if (config.erase && nand.Info().nand_class == fuchsia_hardware_nand_Class_PARTMAP &&
        config.block_num < 24) {
        printf("Erasing the restricted area is not a good idea, sorry\n");
        return false;
    }

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
