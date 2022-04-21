// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/metrics.h"

#include <optional>

namespace minfs {

MinfsMetrics::MinfsMetrics(const fuchsia_minfs::wire::Metrics* metrics) {
  initialized_vmos = metrics->initialized_vmos;
  init_dnum_count = metrics->init_dnum_count;
  init_inum_count = metrics->init_inum_count;
  init_dinum_count = metrics->init_dinum_count;
  init_user_data_size = metrics->init_user_data_size;
  init_user_data_ticks = metrics->init_user_data_ticks;
  vnodes_opened_cache_hit = metrics->vnodes_opened_cache_hit;
  dirty_bytes = metrics->dirty_bytes;
}

void MinfsMetrics::CopyToFidl(fuchsia_minfs::wire::Metrics* metrics) const {
  metrics->initialized_vmos = initialized_vmos.load();
  metrics->init_dnum_count = init_dnum_count.load();
  metrics->init_inum_count = init_inum_count.load();
  metrics->init_dinum_count = init_dinum_count.load();
  metrics->init_user_data_size = init_user_data_size.load();
  metrics->init_user_data_ticks = init_user_data_ticks.load();
  metrics->vnodes_opened_cache_hit = vnodes_opened_cache_hit.load();
  metrics->dirty_bytes = dirty_bytes.load();
}

void MinfsMetrics::Dump(FILE* stream, std::optional<bool> success) const {
  fprintf(stream, "initialized VMOs:                   %lu\n", initialized_vmos.load());
  fprintf(stream, "initialized direct blocks:          %u\n", init_dnum_count.load());
  fprintf(stream, "initialized indirect blocks:        %u\n", init_inum_count.load());
  fprintf(stream, "initialized doubly indirect blocks: %u\n", init_dinum_count.load());
  fprintf(stream, "bytes of files initialized:         %lu\n", init_user_data_size.load());
  fprintf(stream, "ticks during initialization:        %lu\n", init_user_data_ticks.load());
  fprintf(stream, "vnodes open cache hits:             %lu\n", vnodes_opened_cache_hit.load());
  fprintf(stream, "dirty bytes:                        %lu\n", dirty_bytes.load());
}

}  // namespace minfs
