// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_
#define GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "third_party/cobalt/encoder/observation_store.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/logger/event_aggregator.h"
#include "third_party/cobalt/util/consistent_proto_store.h"

namespace cobalt {

class CobaltControllerImpl : public fuchsia::cobalt::Controller {
 public:
  // All of the pointers passed to the constructor must be non-null.
  //
  // There may be more than one ShippingManager because we maintain a
  // ShippingManager for each backend to which we ship Observations.
  //
  // |observation_store| should be the same ObservationStore which is used by
  // by |event_aggregator| and by the Cobalt v1.0 ShippingManager.
  CobaltControllerImpl(async_dispatcher_t* dispatcher,
                       std::vector<encoder::ShippingManager*> shipping_managers,
                       logger::EventAggregator* event_aggregator,
                       encoder::ObservationStore* observation_store);

 private:
  void RequestSendSoon(RequestSendSoonCallback callback) override;

  void BlockUntilEmpty(uint32_t max_wait_seconds,
                       BlockUntilEmptyCallback callback) override;

  void GetNumSendAttempts(GetNumSendAttemptsCallback callback) override;

  void GetFailedSendAttempts(GetFailedSendAttemptsCallback callback) override;

  void GenerateAggregatedObservations(
      uint32_t day_index,
      GenerateAggregatedObservationsCallback callback) override;

  async_dispatcher_t* const dispatcher_;
  std::vector<encoder::ShippingManager*> shipping_managers_;
  logger::EventAggregator* event_aggregator_;
  encoder::ObservationStore* observation_store_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltControllerImpl);
};
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_CONTROLLER_IMPL_H_
