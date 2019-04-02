// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_COBALT_APP_H_
#define GARNET_BIN_COBALT_APP_COBALT_APP_H_

#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/network_wrapper/network_wrapper_impl.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/cobalt_controller_impl.h"
#include "src/cobalt/bin/app/logger_factory_impl.h"
#include "src/cobalt/bin/app/system_data_updater_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/config/project_configs.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/file_observation_store.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/encoder/shuffler_client.h"
#include "third_party/cobalt/logger/encoder.h"
#include "third_party/cobalt/logger/event_aggregator.h"
#include "third_party/cobalt/logger/observation_writer.h"
#include "third_party/cobalt/util/consistent_proto_store.h"

namespace cobalt {

class CobaltApp {
 public:
  // |dispatcher| The async_t to be used for all asynchronous operations.
  //
  // |target_interval| How frequently should the upload scheduler perform
  //                   periodic updates.
  //
  // |min_interval| Because of expedited sends, the upload scheduler thread may
  //                sometimes upload more frequently than |target_interval|.
  //                This parameter is a safety setting. We will never perform
  //                two uploads within a single |min_interval|.
  //
  // |initial_interval| The upload scheduler thread will initially perform more
  //                    frequent uploads at this interval and then exponentially
  //                    back off until it reaches a periodic rhythm of
  //                    |target_interval|.
  //
  // |event_aggregator_backfill_days| The number of past days, in addition to
  //                                  the previous day, for which the
  //                                  EventAggregator generates observations. If
  //                                  a device is unable to generate
  //                                  observations for more than this number of
  //                                  days, we may lose older aggregated data.
  //
  // |start_event_aggregator_worker| If true, starts the EventAggregator's
  //                                 worker thread after constructing the
  //                                 EventAggregator.
  //
  // |use_memory_observation_store| If this is true, the observation stores will
  //                                be in-memory only, otherwise they will be
  //                                file-system backed.
  //
  // |max_bytes_per_observation_store| The maximum number of bytes to store for
  //                                   each of the observation_stores.
  //
  // |product_name| A product name used in the ObservationMetadata sent with
  //                every upload to the Cobalt server.
  //
  // |board_name| A board name that may be used in the ObservationMetadata sent
  //              with every upload to the Cobalt server.
  //
  // REQUIRED:
  //   0 <= min_interval <= target_interval <= kMaxSeconds
  //   0 <= initial_interval <= target_interval
  CobaltApp(
      async_dispatcher_t* dispatcher, std::chrono::seconds target_interval,
      std::chrono::seconds min_interval, std::chrono::seconds initial_interval,
      size_t event_aggregator_backfill_days, bool start_event_aggregator_worker,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name);

 private:
  static encoder::ClientSecret getClientSecret();

  encoder::SystemData system_data_;

  std::unique_ptr<sys::ComponentContext> context_;

  encoder::ShufflerClient shuffler_client_;
  encoder::send_retryer::SendRetryer send_retryer_;
  network_wrapper::NetworkWrapperImpl network_wrapper_;
  std::unique_ptr<encoder::ObservationStore> legacy_observation_store_;
  std::unique_ptr<encoder::ObservationStore> observation_store_;
  std::unique_ptr<util::EncryptedMessageMaker> legacy_encrypt_to_analyzer_;
  std::unique_ptr<util::EncryptedMessageMaker> legacy_encrypt_to_shuffler_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_analyzer_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_shuffler_;
  encoder::LegacyShippingManager legacy_shipping_manager_;
  encoder::ClearcutV1ShippingManager clearcut_shipping_manager_;
  TimerManager timer_manager_;

  util::ConsistentProtoStore local_aggregate_proto_store_;
  util::ConsistentProtoStore obs_history_proto_store_;

  logger::Encoder logger_encoder_;
  logger::ObservationWriter observation_writer_;
  logger::EventAggregator event_aggregator_;

  std::unique_ptr<fuchsia::cobalt::Controller> controller_impl_;
  fidl::BindingSet<fuchsia::cobalt::Controller> controller_bindings_;

  std::unique_ptr<fuchsia::cobalt::LoggerFactory> logger_factory_impl_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> logger_factory_bindings_;

  std::unique_ptr<fuchsia::cobalt::SystemDataUpdater> system_data_updater_impl_;
  fidl::BindingSet<fuchsia::cobalt::SystemDataUpdater>
      system_data_updater_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_COBALT_APP_H_
