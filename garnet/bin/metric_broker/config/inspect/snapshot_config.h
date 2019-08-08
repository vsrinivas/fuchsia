// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_SNAPSHOT_CONFIG_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_SNAPSHOT_CONFIG_H_

namespace broker_service::inspect {

// Provides an in memory representation of an inspect vmo snapshot configuration.
class SnapshotConfig {
 public:
  SnapshotConfig() = delete;
  explicit SnapshotConfig(bool require_consistency_check)
      : require_consistency_check_(require_consistency_check) {}
  SnapshotConfig(const SnapshotConfig&) = delete;
  SnapshotConfig(SnapshotConfig&&) = default;
  SnapshotConfig& operator=(const SnapshotConfig&) = delete;
  SnapshotConfig& operator=(SnapshotConfig&&) = delete;
  ~SnapshotConfig() = default;

  // Returns true if taking a snapshop of this projects |inspect::vmo| should perform
  // consistency checks.
  [[nodiscard]] bool ShouldRequireConsistenfencyCheck() const { return require_consistency_check_; }

 private:
  bool require_consistency_check_;
};

}  // namespace broker_service::inspect

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_SNAPSHOT_CONFIG_H_
