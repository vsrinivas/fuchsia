// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_NAMESPACE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_NAMESPACE_H_

#include <lib/zx/vmo.h>
#include <stdint.h>

#include <vector>

#include "src/devices/block/drivers/nvme-cpp/commands/identify.h"

namespace fake_nvme {

// Configuration for a namespace.
struct NamespaceConfig {
  // List of supported LBA formats.
  std::vector<nvme::LbaFormatField> lba_format{
      nvme::LbaFormatField()
          // Default to 512 byte blocks.
          .set_lba_data_size_log2(9)
          .set_metadata_size_bytes(0)
          .set_relative_performance(nvme::LbaFormatField::RelativePerformance::kBest)};
  // Currently active LBA format.
  uint8_t active_lba_format = 0;
  // Number of blocks in this namespace.
  uint64_t block_count = 1024;
};

class FakeNvmeNamespace {
 public:
  explicit FakeNvmeNamespace() : FakeNvmeNamespace(NamespaceConfig()) {}
  explicit FakeNvmeNamespace(NamespaceConfig config) : config_(std::move(config)) {}

  // Fill in |out| with appropriate values for this namespace.
  void Identify(nvme::IdentifyNvmeNamespace* out);

 private:
  NamespaceConfig config_;
};
}  // namespace fake_nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_FAKE_NVME_NAMESPACE_H_
