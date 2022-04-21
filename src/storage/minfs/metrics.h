// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_MINFS_METRICS_H_
#define SRC_STORAGE_MINFS_METRICS_H_

#include <fidl/fuchsia.minfs/cpp/wire.h>

#include <fbl/macros.h>

namespace minfs {

// TODO(fxbug.dev/98018): Remove MinfsMetrics and replace with Inspect.
class MinfsMetrics {
 public:
  MinfsMetrics() = default;
  explicit MinfsMetrics(const fuchsia_minfs::wire::Metrics* metrics);

  // Not copyable or movable
  MinfsMetrics(const MinfsMetrics&) = delete;
  MinfsMetrics& operator=(const MinfsMetrics&) = delete;
  MinfsMetrics(MinfsMetrics&&) = delete;
  MinfsMetrics& operator=(MinfsMetrics&&) = delete;

  ~MinfsMetrics() = default;

  // Copies to fields of fidl structure the corresponding fields of MinfsMetrics
  void CopyToFidl(fuchsia_minfs::wire::Metrics* metrics) const;

  // Prints the fields of MinfsMetrics and FsMetrics to file |stream|. Passes
  // |success| to FsMetrics::Dump. See FsMetrics::Dump.
  void Dump(FILE* stream, std::optional<bool> success = std::nullopt) const;

  std::atomic<uint64_t> initialized_vmos;
  std::atomic<uint32_t> init_dnum_count;  // Top-level direct blocks only
  std::atomic<uint32_t> init_inum_count;  // Top-level indirect blocks only
  std::atomic<uint32_t> init_dinum_count;
  std::atomic<uint64_t> init_user_data_size;
  std::atomic<uint64_t> init_user_data_ticks;
  std::atomic<uint64_t> vnodes_opened_cache_hit;
  std::atomic<uint64_t> dirty_bytes;
};
}  // namespace minfs
#endif  // SRC_STORAGE_MINFS_METRICS_H_
