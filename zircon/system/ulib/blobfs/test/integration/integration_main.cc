// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <zxtest/zxtest.h>

#include "environment.h"

constexpr char kUsageMessage[] = R"""(
Blobfs integration tests. Tests can be run either against a real block device
or using a ram-disk (default behavior).

Options:
--device path_to_device (-d): Performs tests on top of a specific block device
--no-journal: Don't use journal
--help (-h): Displays full help

)""";

bool GetOptions(int argc, char** argv, Environment::TestConfig* config) {
  while (true) {
    struct option options[] = {
        {"device", required_argument, nullptr, 'd'},
        {"no-journal", no_argument, nullptr, 'j'},
        {"help", no_argument, nullptr, 'h'},
        {"gtest_filter", optional_argument, nullptr, 'f'},
        {"gtest_list_tests", optional_argument, nullptr, 'l'},
        {"gtest_shuffle", optional_argument, nullptr, 's'},
        {"gtest_repeat", required_argument, nullptr, 'i'},
        {"gtest_random_seed", required_argument, nullptr, 'r'},
        {"gtest_break_on_failure", optional_argument, nullptr, 'b'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "d:hf::l::s::i:r:b::", options, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        config->path = optarg;
        break;
      case 'j':
        config->use_journal = false;
        break;
      case 'h':
        printf("%s\n", kUsageMessage);
        return true;
      case '?':
        return false;
    }
  }
  return argc == optind;
}

// The test can operate over either a ramdisk, or a real device. Initialization
// of that device happens at the test environment level, but the test fixtures
// must be able to see it.
Environment* g_environment;

int main(int argc, char** argv) {
  Environment::TestConfig config = {};
  if (!GetOptions(argc, argv, &config)) {
    printf("%s\n", kUsageMessage);
    return -1;
  }

  auto parent = std::make_unique<Environment>(config);
  g_environment = parent.get();

  // Initialize a tmpfs instance to "hold" the mounted blobfs.
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  if (loop.StartThread() != ZX_OK) {
    printf("Unable to initialize local tmpfs loop\n");
    return -1;
  }
  if (memfs_install_at(loop.dispatcher(), "/blobfs-tmp") != ZX_OK) {
    printf("Unable to install local tmpfs\n");
    return -1;
  }

  zxtest::Runner::GetInstance()->AddGlobalTestEnvironment(std::move(parent));

  return RUN_ALL_TESTS(argc, argv);
}
