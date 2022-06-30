// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/fake/fake-nvme-namespace.h"

namespace fake_nvme {

void FakeNvmeNamespace::Identify(nvme::IdentifyNvmeNamespace *out) {
  out->n_lba_f = static_cast<uint8_t>(config_.lba_format.size());
  for (uint8_t i = 0; i < out->n_lba_f; i++) {
    out->lba_formats[i] = config_.lba_format[i];
  }
  out->set_lba_format_index_lo(config_.active_lba_format);
  out->set_lba_format_index_hi(config_.active_lba_format >> 4);
  out->set_lba_metadata_mode(0);
  out->n_sze = config_.block_count;
}

}  // namespace fake_nvme
