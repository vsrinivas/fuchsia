// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>

#include <zxtest/zxtest.h>

#include "parent.h"

constexpr char kUsageMessage[] = R"""(
Basic functionality test for a nand device.
WARNING: Will write to the nand device.

Broker unit test:
  ./nand-test

  Creates a ram-nand device and runs all the test against it.

Existing nand device:
  ./nand-test --device path_to_device --first-block 100 --num-blocks 10

  Opens the provided nand device and uses blocks [100, 109] to perform tests.
  Note that this doesn't verify all the blocks in the given range, just makes
  sure no block outside of that range is modified.

Existing broker device:
  ./nand-test --device path_to_device --broker --first-block 100 --num-blocks 10

  Opens the provided broker device and uses blocks [100, 109] to perform tests.
  Note that this doesn't verify all the blocks in the given range, just makes
  sure no block outside of that range is modified.

--device path_to_device
  Performs tests over an existing stack.

--broker
  The device to attach to is not a nand device, but a broker.

--first-block n
  The fist block that can be written from an existing device.

--num-blocks n
  The number of blocks that can be written, after first-block.

)""";

const fuchsia_hardware_nand_Info kDefaultNandInfo = {.page_size = 4096,
                                                     .pages_per_block = 4,
                                                     .num_blocks = 5,
                                                     .ecc_bits = 6,
                                                     .oob_size = 4,
                                                     .nand_class = fuchsia_hardware_nand_Class_TEST,
                                                     .partition_guid = {}};

// The test can operate over either a ram-nand, or a real device. The simplest
// way to control what's going on is to have a place outside the test framework
// that controls where to execute, as "creation / teardown" of the external
// device happens at the process level.
ParentDevice* g_parent_device_;

int main(int argc, char** argv) {
  ParentDevice::TestConfig config = {};
  config.info = kDefaultNandInfo;

  while (true) {
    struct option options[] = {
        {"device", required_argument, nullptr, 'd'},
        {"broker", no_argument, nullptr, 'b'},
        {"first-block", required_argument, nullptr, 'f'},
        {"num-blocks", required_argument, nullptr, 'n'},
        {"help", no_argument, nullptr, 'h'},
        {"list", no_argument, nullptr, 'l'},
        {"case", required_argument, nullptr, 'c'},
        {"test", required_argument, nullptr, 't'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "d:bhlc:t:", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        config.path = optarg;
        break;
      case 'b':
        config.is_broker = true;
        break;
      case 'f':
        config.first_block = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'n':
        config.num_blocks = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
        break;
      case 'h':
        printf("%s\n", kUsageMessage);
        break;
    }
  }

  if (config.first_block && !config.num_blocks) {
    printf("num-blocks required when first-block is set\n");
    return -1;
  }

  ParentDevice parent(config);
  if (!parent.IsValid()) {
    printf("Unable to open the nand device\n");
    return -1;
  }

  if (config.path && !config.first_block) {
    printf("About to overwrite device. Press y to confirm.\n");
    int c = getchar();
    if (c != 'y') {
      return -1;
    }
  }

  g_parent_device_ = &parent;

  return RUN_ALL_TESTS(argc, argv);
}
