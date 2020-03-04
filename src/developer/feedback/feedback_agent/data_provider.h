// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>

#include <cstdint>
#include <functional>
#include <memory>

#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/datastore.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/timekeeper/clock.h"
#include "src/lib/timekeeper/system_clock.h"

namespace feedback {

// Provides data useful to attach in feedback reports (crash, user feedback or bug reports).
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  // Static factory method.
  //
  // Returns nullptr if the data provider cannot be instantiated, e.g., because the config cannot be
  // parsed.
  static std::unique_ptr<DataProvider> TryCreate(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services);

  DataProvider(
      async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
      const Config& config,
      std::unique_ptr<timekeeper::Clock> clock = std::make_unique<timekeeper::SystemClock>());

  // |fuchsia.feedback.DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override;

  // Irreversibly shuts down an instance of |DataProvider|.
  void Shutdown();

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const Config config_;
  Cobalt cobalt_;
  async::Executor executor_;
  Datastore datastore_;

  bool shut_down_ = false;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
