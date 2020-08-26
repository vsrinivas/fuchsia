// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/data_provider_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace crash_reports {

using fuchsia::feedback::Snapshot;

DataProviderPtr::DataProviderPtr(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 zx::duration pool_delta, std::unique_ptr<timekeeper::Clock> clock)
    : services_(services),
      pending_calls_(dispatcher),
      pool_delta_(pool_delta),
      clock_(std::move(clock)) {}

::fit::promise<Snapshot, Error> DataProviderPtr::GetSnapshot(const zx::duration timeout) {
  if (!connection_) {
    Connect();
  }

  const zx::time current_time(clock_->Now());
  const uint64_t pending_call_id = pending_calls_.NewBridgeForTask("Snapshot retrieval");

  std::string pool_id;
  if (!pools_.empty() && (current_time < pools_[latest_pool_uuid_].creation_time + pool_delta_)) {
    // Set |pool_id| to the most recent pool and add the pending call to that pool.
    pool_id = latest_pool_uuid_;

    pools_[pool_id].pending_call_ids.push_back(pending_call_id);
    ++(pools_[pool_id].max_pool_size);
  } else {
    // Create a new pool and request the snapshot.
    pool_id = uuid::Generate();
    pools_[pool_id] = Pool{
        .creation_time = current_time,
        .pending_call_ids = std::vector<uint64_t>({pending_call_id}),
        .max_pool_size = 1,
    };
    latest_pool_uuid_ = pool_id;

    connection_->GetSnapshot(
        // We give 15s for the packaging of the snapshot and the round-trip between the client and
        // the server and the rest is given to each data collection.
        std::move(fuchsia::feedback::GetSnapshotParameters().set_collection_timeout_per_data(
            (timeout - zx::sec(15) /* cost of making the call and packaging the snapshot */)
                .get())),
        [pending_call_id, pool_id, this](Snapshot snapshot) {
          if (pending_calls_.IsAlreadyDone(pending_call_id)) {
            return;
          }

          FX_CHECK(pools_.find(pool_id) != pools_.end());

          // Complete all of the bridges in pool |pool_id| with |snapshot|.
          for (const auto& id : pools_[pool_id].pending_call_ids) {
            Snapshot clone;
            if (const zx_status_t status = snapshot.Clone(&clone); status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Failed to clone snapshot";
              continue;
            }

            pending_calls_.CompleteOk(id, std::move(clone));
          }
        });
  }

  return pending_calls_.WaitForDone(pending_call_id, fit::Timeout(timeout))
      .then([pending_call_id, pool_id, this](::fit::result<Snapshot, Error>& result) {
        // We need to use the result before erasing the bridge because |result| is passed as a
        // reference.
        Snapshot snapshot;
        if (result.is_ok()) {
          snapshot = result.take_value();
        } else {
          snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
              .key = "debug.snapshot.error",
              .value = ToReason(result.error()),
          });
        }

        pending_calls_.Delete(pending_call_id);

        // Close the connection if we were the last pending call to GetSnapshot().
        if (pending_calls_.IsEmpty()) {
          connection_.Unbind();
        }

        FX_CHECK(pools_.find(pool_id) != pools_.end());
        auto& pool = pools_[pool_id];

        // If |snapshot| is empty, add an annotation indicating so.
        if (snapshot.IsEmpty()) {
          snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
              .key = "debug.snapshot.empty",
              .value = "true",
          });
        }

        // Augment |snapshot| with the pool size, UUID, and delta.
        snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
            .key = "debug.snapshot.pool.size",
            .value = std::to_string(pool.max_pool_size),
        });
        snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
            .key = "debug.snapshot.pool.uuid",
            .value = pool_id,
        });
        snapshot.mutable_annotations()->push_back(fuchsia::feedback::Annotation{
            .key = "debug.snapshot.pool.delta-seconds",
            .value = std::to_string(pool_delta_.to_secs()),
        });

        // Remove |pending_call_id| from the pool.
        pool.pending_call_ids.erase(
            std::find(pool.pending_call_ids.begin(), pool.pending_call_ids.end(), pending_call_id));

        // If the pool is empty, delete it.
        if (pool.pending_call_ids.empty()) {
          pools_.erase(pool_id);
        }

        return ::fit::make_result_promise<Snapshot, Error>(::fit::ok(std::move(snapshot)));
      });
}

void DataProviderPtr::Connect() {
  if (connection_) {
    return;
  }

  connection_ = services_->Connect<fuchsia::feedback::DataProvider>();

  connection_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.feedback.DataProvider";

    pending_calls_.CompleteAllError(Error::kConnectionError);
  });
}

}  // namespace crash_reports
}  // namespace forensics
