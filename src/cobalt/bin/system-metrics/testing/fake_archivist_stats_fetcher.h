
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVIST_STATS_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVIST_STATS_FETCHER_H_

#include <lib/async/dispatcher.h>

#include "src/cobalt/bin/system-metrics/archivist_stats_fetcher.h"

namespace cobalt {

class FakeArchivistStatsFetcher : public ArchivistStatsFetcher {
 public:
  explicit FakeArchivistStatsFetcher(async_dispatcher_t* dispatcher);

  void AddMeasurement(Measurement measurement);

  // Overridden from ArchivistStatsFetcher:
  void FetchMetrics(MetricsCallback metrics_callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  std::vector<Measurement> measurements_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVIST_STATS_FETCHER_H_
