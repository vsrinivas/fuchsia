// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/as370.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/nelson.h"
#include "src/storage/lib/paver/pinecrest.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/vim3.h"
#include "src/storage/lib/paver/x64.h"

int main(int argc, char** argv) {
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::AstroPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::As370PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::NelsonPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::SherlockPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::LuisPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::Vim3PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(
      std::make_unique<paver::ChromebookX64PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::PinecrestPartitionerFactory>());

  // X64PartitionerFactory must be placed last if test will be run on x64 devices.
  // This is because X64PartitionerFactory determines whether itself is suitable to be used for the
  // device based on arch hardcoded at compile time. It will always be the case for x64 devices.
  // The initialization will update to x64 GPT table, which can confuse paver test for other
  // boards.
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::X64PartitionerFactory>());

  paver::DevicePartitionerFactory::Register(std::make_unique<paver::DefaultPartitionerFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::AstroAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::NelsonAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::SherlockAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::LuisAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::Vim3AbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::PinecrestAbrClientFactory>());

  // Same as X64PartitionerFactory, needs to place last.
  abr::ClientFactory::Register(std::make_unique<paver::X64AbrClientFactory>());

  return RUN_ALL_TESTS(argc, argv);
}
