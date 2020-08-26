// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATA_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATA_PROVIDER_PTR_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/bridge_map.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace crash_reports {

// Wraps around fuchsia::feedback::DataProviderPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// Manages the lifetime of GetSnapshot() calls by returning the same snapshot for
// calls that happen within |pool_delta_| time of one another.
class DataProviderPtr {
 public:
  DataProviderPtr(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  zx::duration pool_delta, std::unique_ptr<timekeeper::Clock> clock);

  ::fit::promise<fuchsia::feedback::Snapshot, Error> GetSnapshot(zx::duration timeout);

 private:
  struct Pool {
    // When the pool was created – this is useful to only add new requests to the latest pool if it
    // was not created too long ago.
    zx::time creation_time;

    // All the pending calls in |pending_calls_| that need to be executed at once when the snapshot
    // is returned or the time out is up.
    std::vector<uint64_t> pending_call_ids;

    // The maximum size of |pending_call_ids|. This is needed because values are asynchronously
    // removed from |pending_call_ids| despite the pool size still being necessary.
    size_t max_pool_size;
  };

  void Connect();

  const std::shared_ptr<sys::ServiceDirectory> services_;

  fuchsia::feedback::DataProviderPtr connection_;
  fit::BridgeMap<fuchsia::feedback::Snapshot> pending_calls_;

  zx::duration pool_delta_;
  std::unique_ptr<timekeeper::Clock> clock_;

  // Map of unique ids to Pools.
  std::map<std::string, Pool> pools_;
  std::string latest_pool_uuid_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_DATA_PROVIDER_PTR_H_
