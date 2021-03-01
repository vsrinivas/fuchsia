// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/minfs_private.h"
#ifdef FS_WITH_METRICS
#include <storage-metrics/storage-metrics.h>

#include "src/storage/minfs/metrics.h"
#endif

namespace minfs {

#ifdef FS_WITH_METRICS
MinfsMetrics::MinfsMetrics(const ::llcpp::fuchsia::minfs::wire::Metrics* metrics)
    : FsMetrics::FsMetrics(&metrics->fs_metrics) {
  initialized_vmos = metrics->initialized_vmos;
  init_dnum_count = metrics->init_dnum_count;
  init_inum_count = metrics->init_inum_count;
  init_dinum_count = metrics->init_dinum_count;
  init_user_data_size = metrics->init_user_data_size;
  init_user_data_ticks = metrics->init_user_data_ticks;
  vnodes_opened_cache_hit = metrics->vnodes_opened_cache_hit;
  dirty_bytes = metrics->dirty_bytes;
}

void MinfsMetrics::CopyToFidl(::llcpp::fuchsia::minfs::wire::Metrics* metrics) const {
  FsMetrics::CopyToFidl(&metrics->fs_metrics);

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
  FsMetrics::Dump(stream, success);

  fprintf(stream, "initialized VMOs:                   %lu\n", initialized_vmos.load());
  fprintf(stream, "initialized direct blocks:          %u\n", init_dnum_count.load());
  fprintf(stream, "initialized indirect blocks:        %u\n", init_inum_count.load());
  fprintf(stream, "initialized doubly indirect blocks: %u\n", init_dinum_count.load());
  fprintf(stream, "bytes of files initialized:         %lu\n", init_user_data_size.load());
  fprintf(stream, "ticks during initialization:        %lu\n", init_user_data_ticks.load());
  fprintf(stream, "vnodes open cache hits:             %lu\n", vnodes_opened_cache_hit.load());
  fprintf(stream, "dirty bytes:                        %lu\n", dirty_bytes.load());
}
#endif  // FS_WITH_METRICS

void Minfs::UpdateInitMetrics(uint32_t dnum_count, uint32_t inum_count, uint32_t dinum_count,
                              uint64_t user_data_size, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  if (metrics_.Enabled()) {
    metrics_.initialized_vmos++;
    metrics_.init_user_data_size += user_data_size;
    metrics_.init_user_data_ticks += duration.get();
    metrics_.init_dnum_count += dnum_count;
    metrics_.init_inum_count += inum_count;
    metrics_.init_dinum_count += dinum_count;
  }
#endif
}

void Minfs::UpdateLookupMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateLookupStat(success, duration.get(), uint64_t(0));
#endif
}

void Minfs::UpdateCreateMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateCreateStat(success, duration.get(), 0);
#endif
}

void Minfs::UpdateReadMetrics(uint64_t size, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateReadStat(true, duration.get(), size);
#endif
}

void Minfs::UpdateWriteMetrics(uint64_t size, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateWriteStat(true, duration.get(), size);
#endif
}

void Minfs::UpdateTruncateMetrics(const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateTruncateStat(true, duration.get(), 0);
#endif
}

void Minfs::UpdateUnlinkMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateUnlinkStat(success, duration.get(), 0);
#endif
}

void Minfs::UpdateRenameMetrics(bool success, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateRenameStat(success, duration.get(), 0);
#endif
}

void Minfs::UpdateOpenMetrics(bool cache_hit, const fs::Duration& duration) {
#ifdef FS_WITH_METRICS
  metrics_.UpdateOpenStat(true, duration.get(), minfs::kMinfsBlockSize);
  metrics_.vnodes_opened_cache_hit += cache_hit ? 1 : 0;
#endif
}

}  // namespace minfs
