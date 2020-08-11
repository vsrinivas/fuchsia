// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SNAPSHOT_REQUEST_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SNAPSHOT_REQUEST_MANAGER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <vector>

#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Manages the lifetime of requests for snapshots by returning the same snapshot for
// requests that happen within |delta_| time of one another.
class SnapshotRequestManager {
 public:
  SnapshotRequestManager(zx::duration delta, std::unique_ptr<timekeeper::Clock> clock);

  // Manages a snapshot request, defined by its timeout and callback.
  //
  // Returns std::nullopt if there is a pending recent similar request for which the manager will
  // respond with the same snapshot once generated. Otherwise returns a new ID for the client to
  // use in Respond() once it has generated a snapshot.
  std::optional<uint64_t> Manage(
      zx::duration request_timeout,
      fuchsia::feedback::DataProvider::GetSnapshotCallback request_callback);

  // Returns the same |snapshot| for all callbacks pooled under the same |id|.
  void Respond(uint64_t id, fuchsia::feedback::Snapshot snapshot);

 private:
  struct CallbackPool {
    // A unique id for the pool.
    uint64_t id;

    // When the pool was created – this is useful to only add new requests to the latest pool if it
    // was not created too long ago.
    zx::time creation_time;

    // The timeout shared by all the requests in the pool – this avoids having to pool
    // together requests with different timeouts.
    zx::duration request_timeout;

    // All the requests' callbacks that will be called at once when the snapshot is generated.
    std::vector<fuchsia::feedback::DataProvider::GetSnapshotCallback> callbacks;
  };

  zx::duration delta_;
  std::unique_ptr<timekeeper::Clock> clock_;

  std::vector<CallbackPool> pools_;
  uint64_t next_pool_id_{0};
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SNAPSHOT_REQUEST_MANAGER_H_
