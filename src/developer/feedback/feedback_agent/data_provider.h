// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <functional>
#include <memory>

#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/ref_counted_delayed_task.h"
#include "src/developer/feedback/utils/cobalt.h"

namespace feedback {

// Provides data useful to attach in feedback reports (crash or user feedback).
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  // Static factory method.
  //
  // |after_timeout| is executed if a duration of greater than |timeout| passes since the last call
  // to this component by a client.
  //
  // Returns nullptr if the data provider cannot be instantiated, e.g., because the config cannot be
  // parsed.
  static std::unique_ptr<DataProvider> TryCreate(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 std::function<void()> after_timeout,
                                                 zx::duration timeout);

  DataProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
               const Config& config, std::function<void()> after_timeout, zx::duration timeout);

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
  RefCountedDelayedTask after_timeout_;
  async::Executor executor_;

  bool shut_down_ = false;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
