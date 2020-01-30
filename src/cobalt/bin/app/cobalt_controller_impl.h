// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_COBALT_CONTROLLER_IMPL_H_
#define SRC_COBALT_BIN_APP_COBALT_CONTROLLER_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "third_party/cobalt/src/local_aggregation/event_aggregator_mgr.h"
#include "third_party/cobalt/src/observation_store/observation_store.h"
#include "third_party/cobalt/src/public/cobalt_service.h"
#include "third_party/cobalt/src/uploader/shipping_manager.h"

namespace cobalt {

class CobaltControllerImpl : public fuchsia::cobalt::Controller {
 public:
  // All of the pointers passed to the constructor must be non-null.
  CobaltControllerImpl(async_dispatcher_t* dispatcher, CobaltService* cobalt_service);

 private:
  void RequestSendSoon(RequestSendSoonCallback callback) override;

  void BlockUntilEmpty(uint32_t max_wait_seconds, BlockUntilEmptyCallback callback) override;

  void GetNumSendAttempts(GetNumSendAttemptsCallback callback) override;

  void GetFailedSendAttempts(GetFailedSendAttemptsCallback callback) override;

  void GetNumObservationsAdded(GetNumObservationsAddedCallback callback) override;

  void GetNumEventAggregatorRuns(GetNumEventAggregatorRunsCallback callback) override;

  void GenerateAggregatedObservations(uint32_t day_index, std::vector<uint32_t> report_ids,
                                      GenerateAggregatedObservationsCallback callback) override;

  async_dispatcher_t* const dispatcher_;
  CobaltService* cobalt_service_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltControllerImpl);
};
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_COBALT_CONTROLLER_IMPL_H_
