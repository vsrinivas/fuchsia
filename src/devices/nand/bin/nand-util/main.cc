// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <stdio.h>

#include "aml.h"
#include "commands.h"
#include "ftl.h"
#include "nand-broker.h"

namespace {

constexpr char kUsageMessage[] = R"""(
Low level access tool for a NAND device.
WARNING: This tool may overwrite the NAND device.

nand-util --device /dev/sys/platform/05:00:f/aml-raw_nand/nand/fvm --info

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
  --wear : print wear counts.
  --file (-f) path:  Path to use when saving data.
  --absolute (-a) xxx : Use an absolute page number.
  --page (-p) xxx : Use the xxx page number (from within a block).
  --block (-b) xxx : Use the xxx block number (0-based).
  --count (-n) xxx : Limit the operation to xxx blocks.
                     Only supported with --check, --erase and --save.
  --no-ftl : Don't attempt to interpret FTL data.
  --live-dangerously : Don't prompt for confirmation.
)""";

enum class Actions { kBbt, kRead, kErase, kReadCheck, kSave, kWear };

// Configuration info (what to do).
struct Config {
  const char* path;
  const char* file;
  uint32_t page_num;
  uint32_t block_num;
  uint32_t abs_page;
  uint32_t count;
  Actions action;
  int num_actions;
  bool info;
  bool skip_prompt;
  bool ignore_ftl;
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
        {"wear", no_argument, nullptr, 'x'},
        {"file", required_argument, nullptr, 'f'},
        {"page", required_argument, nullptr, 'p'},
        {"block", required_argument, nullptr, 'b'},
        {"absolute", required_argument, nullptr, 'a'},
        {"count", required_argument, nullptr, 'n'},
        {"no-ftl", no_argument, nullptr, 'z'},
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
        config->action = Actions::kBbt;
        config->num_actions++;
        break;
      case 'r':
        config->action = Actions::kRead;
        config->num_actions++;
        break;
      case 'e':
        config->action = Actions::kErase;
        config->num_actions++;
        break;
      case 'c':
        config->action = Actions::kReadCheck;
        config->num_actions++;
        break;
      case 's':
        config->action = Actions::kSave;
        config->num_actions++;
        break;
      case 'x':
        config->action = Actions::kWear;
        config->num_actions++;
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
      case 'z':
        config->ignore_ftl = true;
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

  if (config.num_actions > 1) {
    printf("Only one action allowed\n");
    return false;
  }

  if (config.abs_page && config.page_num) {
    printf("Provide either a block + page or an absolute page number\n");
    return false;
  }

  if ((config.action == Actions::kErase || config.action == Actions::kSave) &&
      (config.page_num || config.abs_page)) {
    printf("The operation works with blocks, not pages\n");
    return false;
  }

  if (!config.info && !config.num_actions) {
    printf("Nothing to do\n");
    return false;
  }

  if (config.action == Actions::kSave && !config.file) {
    printf("Save requires a file\n");
    return false;
  }

  if (config.count && (config.action != Actions::kReadCheck && config.action != Actions::kSave &&
                       config.action != Actions::kErase)) {
    printf("Count not supported for this operation\n");
    return false;
  }
  return true;
}

bool ValidateOptionsWithNand(const NandBroker& nand, const Config& config) {
  if (config.action == Actions::kBbt) {
    return true;
  }

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

  if (config.action == Actions::kErase &&
      nand.Info().nand_class == fuchsia_hardware_nand_Class_PARTMAP && config.block_num < 24) {
    printf("Erasing the restricted area is not a good idea, sorry\n");
    return false;
  }

  return true;
}

bool ExecuteAction(const NandBroker& nand, const Config& config) {
  if (!config.num_actions) {
    return true;
  }

  switch (config.action) {
    case Actions::kBbt:
      return FindBadBlocks(nand);

    case Actions::kRead: {
      uint32_t abs_page = config.abs_page
                              ? config.abs_page
                              : config.block_num * nand.Info().pages_per_block + config.page_num;
      printf("To read page %d\n", abs_page);
      return nand.DumpPage(abs_page);
    }

    case Actions::kErase: {
      // Erase a single block by default.
      uint32_t count = config.count ? config.count : 1;
      if (!config.skip_prompt) {
        printf("About to erase %d block(s) starting at block %d. Press y to confirm\n", count,
               config.block_num);
        if (getchar() != 'y') {
          return false;
        }
      }
      return Erase(nand, config.block_num, count);
    }

    case Actions::kReadCheck:
      printf("Checking blocks...\n");
      return ReadCheck(nand, config.block_num, config.count);

    case Actions::kSave:
      printf("Saving blocks...\n");
      return Save(nand, config.block_num, config.count, config.file);

    case Actions::kWear:
      return WearCounts(nand);
  }
  return false;
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

  if (!ValidateOptionsWithNand(nand, config)) {
    nand.ShowInfo();
    return -1;
  }

  if (!config.ignore_ftl) {
    nand.SetFtl(FtlInfo::Factory(&nand));
  }

  if (config.info) {
    nand.ShowInfo();
    if (!nand.ReadPages(0, 1)) {
      return -1;
    }
    DumpPage0(nand.data());

    if (nand.ftl()) {
      nand.ftl()->DumpInfo();
    }
  }

  return ExecuteAction(nand, config) ? 0 : -1;
}
