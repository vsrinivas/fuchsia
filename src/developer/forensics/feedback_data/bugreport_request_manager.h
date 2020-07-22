// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_BUGREPORT_REQUEST_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_BUGREPORT_REQUEST_MANAGER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <vector>

#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {

// Manages the lifetime of requests for bugreports by returning the same bugreport for
// requests that happen within |delta_| time of one another.
class BugreportRequestManager {
 public:
  BugreportRequestManager(zx::duration delta, std::unique_ptr<timekeeper::Clock> clock);

  // Manages a bugreport request, defined by its timeout and callback.
  //
  // Returns std::nullopt if there is a pending recent similar request for which the manager will
  // respond with the same bugreport once generated. Otherwise returns a new ID for the client to
  // use in Respond() once it has generated a bugreport.
  std::optional<uint64_t> Manage(
      zx::duration request_timeout,
      fuchsia::feedback::DataProvider::GetBugreportCallback request_callback);

  // Returns the same |bugreport| for all callbacks pooled under the same |id|.
  void Respond(uint64_t id, fuchsia::feedback::Bugreport bugreport);

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

    // All the requests' callbacks that will be called at once when the bugreport is generated.
    std::vector<fuchsia::feedback::DataProvider::GetBugreportCallback> callbacks;
  };

  zx::duration delta_;
  std::unique_ptr<timekeeper::Clock> clock_;

  std::vector<CallbackPool> pools_;
  uint64_t next_pool_id_{0};
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_BUGREPORT_REQUEST_MANAGER_H_
