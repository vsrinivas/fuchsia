// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_COBALT_APP_H_
#define SRC_COBALT_BIN_APP_COBALT_APP_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/cobalt_controller_impl.h"
#include "src/cobalt/bin/app/configuration_data.h"
#include "src/cobalt/bin/app/logger_factory_impl.h"
#include "src/cobalt/bin/app/system_data_updater_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "src/cobalt/bin/utils/clock.h"
#include "src/lib/network_wrapper/network_wrapper_impl.h"
#include "third_party/cobalt/src/lib/util/consistent_proto_store.h"
#include "third_party/cobalt/src/local_aggregation/event_aggregator_mgr.h"
#include "third_party/cobalt/src/logger/encoder.h"
#include "third_party/cobalt/src/logger/observation_writer.h"
#include "third_party/cobalt/src/logger/undated_event_manager.h"
#include "third_party/cobalt/src/observation_store/file_observation_store.h"
#include "third_party/cobalt/src/registry/project_configs.h"
#include "third_party/cobalt/src/system_data/client_secret.h"
#include "third_party/cobalt/src/system_data/system_data.h"
#include "third_party/cobalt/src/uploader/shipping_manager.h"

namespace cobalt {

// Main app, which manages all of Cobalt's functionality.
//
// To test run:
//    fx set --with-base //bundles:tools,//src/cobalt/bin:cobalt_tests;
//    fx run-test-component cobalt_testapp_no_network
class CobaltApp {
 public:
  // |dispatcher| The async_t to be used for all asynchronous operations.
  //
  // |target_interval| How frequently should the upload scheduler perform periodic updates.
  //
  // |min_interval| Because of expedited sends, the upload scheduler thread may sometimes upload
  //                more frequently than |target_interval|. This parameter is a safety setting. We
  //                will never perform two uploads within a single |min_interval|.
  //
  // |initial_interval| The upload scheduler thread will initially perform more
  //                    frequent uploads at this interval and then exponentially
  //                    back off until it reaches a periodic rhythm of
  //                    |target_interval|.
  //
  // |event_aggregator_backfill_days| The number of past days, in addition to the previous day, for
  //                                  which local aggregation generates observations. If  a device
  //                                  is unable to generate observations for more than this number
  //                                  of days, we may lose older aggregated data.
  //
  // |start_event_aggregator_worker| If true, starts the EventAggregatorManager's worker thread
  //                                 after constructing the EventAggregatorManager.
  //
  // |use_memory_observation_store| If this is true, the observation stores will be in-memory only,
  //                                otherwise they will be file-system backed.
  //
  // |max_bytes_per_observation_store| The maximum number of bytes to store for each of the
  //                                   observation_stores.
  //
  // |product_name| A product name included in the SystemProfile that is implicitly part of every
  //                Cobalt metric.
  //
  //                Example: products/core.gni
  //
  // |board_name| A board name that may be included in the SystemProfile that is implicitly part of
  //              every Cobalt metric.
  //
  //              Examples: astro, vim2, qemu
  //
  // |version| The version of the running system included in the SystemProfile that is implicitly
  //           part of every Cobalt metric.
  //
  //           Example: 20190220_01_RC00
  //
  // REQUIRED:
  //   0 <= min_interval <= target_interval <= kMaxSeconds
  //   0 <= initial_interval <= target_interval
  CobaltApp(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
            std::chrono::seconds target_interval, std::chrono::seconds min_interval,
            std::chrono::seconds initial_interval, size_t event_aggregator_backfill_days,
            bool start_event_aggregator_worker, bool use_memory_observation_store,
            size_t max_bytes_per_observation_store, const std::string& product_name,
            const std::string& board_name, const std::string& version);

 private:
  static encoder::ClientSecret getClientSecret();

  // Creates a Logger which will be used for logging Cobalt's internal metrics.
  std::unique_ptr<logger::Logger> NewInternalLogger(
      const std::shared_ptr<logger::ProjectContextFactory> global_project_context_factory,
      const std::string& customer_name, const std::string& project_name,
      ReleaseStage release_stage);

  FuchsiaConfigurationData configuration_data_;
  encoder::SystemData system_data_;

  std::unique_ptr<sys::ComponentContext> context_;

  FuchsiaSystemClock system_clock_;

  network_wrapper::NetworkWrapperImpl network_wrapper_;
  std::unique_ptr<observation_store::ObservationStore> observation_store_;
  std::unique_ptr<util::EncryptedMessageMaker> encrypt_to_analyzer_;
  std::vector<std::unique_ptr<util::EncryptedMessageMaker>> encrypt_to_shufflers_;
  std::unique_ptr<encoder::ShippingManager> shipping_manager_;
  TimerManager timer_manager_;

  util::ConsistentProtoStore local_aggregate_proto_store_;
  util::ConsistentProtoStore obs_history_proto_store_;

  logger::Encoder logger_encoder_;
  std::unique_ptr<logger::ObservationWriter> observation_writer_;
  std::unique_ptr<local_aggregation::EventAggregatorManager> event_aggregator_manager_;
  std::shared_ptr<logger::UndatedEventManager> undated_event_manager_;

  std::unique_ptr<fuchsia::cobalt::Controller> controller_impl_;
  fidl::BindingSet<fuchsia::cobalt::Controller> controller_bindings_;

  std::unique_ptr<fuchsia::cobalt::LoggerFactory> logger_factory_impl_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> logger_factory_bindings_;

  std::unique_ptr<fuchsia::cobalt::SystemDataUpdater> system_data_updater_impl_;
  fidl::BindingSet<fuchsia::cobalt::SystemDataUpdater> system_data_updater_bindings_;

  // Cobalt uses internal_logger_ to log events about Cobalt.
  std::unique_ptr<logger::Logger> internal_logger_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_COBALT_APP_H_
