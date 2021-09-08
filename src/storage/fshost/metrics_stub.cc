// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/metrics.h"

namespace fshost {

class FsHostMetricsStub : public FsHostMetrics {
 public:
  void LogMinfsCorruption() override {}

  void Flush() override {}
};

std::unique_ptr<FsHostMetrics> DefaultMetrics() { return std::make_unique<FsHostMetricsStub>(); }

}  // namespace fshost
