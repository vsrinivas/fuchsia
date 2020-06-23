// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_IMPL_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/sys/cpp/component_context.h>

#include <map>

#include "src/cobalt/bin/system-metrics/archivist_stats_fetcher.h"

namespace cobalt {

// Implementation of archivist stats fetcher.
//
// This class is NOT thread safe.
class ArchivistStatsFetcherImpl : public ArchivistStatsFetcher {
 public:
  ArchivistStatsFetcherImpl(async_dispatcher_t* dispatcher, sys::ComponentContext* context);
  void FetchMetrics(MetricsCallback metrics_callback) override;

 protected:
  ArchivistStatsFetcherImpl(async_dispatcher_t* dispatcher,
                            fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector);

 private:
  void ProcessNewValue(MeasurementKey key, MeasurementValue value, const MetricsCallback& callback);

  // Gets the difference between the given value and the previous measurement for the key, if it
  // exists. If the previous measurement does not exist, the original value is returned.
  MeasurementValue GetDifferenceForMetric(const MeasurementKey& key, MeasurementValue value);

  // Upsert the new value for the given key.
  void UpdatePreviousValue(const MeasurementKey& key, MeasurementValue value);

  // An executor on which to run promises.
  async::Executor executor_;

  // Function to get a new Archive connection.
  fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector_;

  // Mapping of parameters to the previous measurement of those parameters.
  std::map<MeasurementKey, MeasurementValue> previous_measurements_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_ARCHIVIST_STATS_FETCHER_IMPL_H_
