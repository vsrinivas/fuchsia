// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/async_promise/executor.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/feedback_agent/config.h"

namespace fuchsia {
namespace feedback {

// Provides data useful to attach in feedback reports (crash or user feedback).
class DataProviderImpl : public DataProvider {
 public:
  // Static factory method.
  // Returns nullptr if the data provider cannot be instantiated, e.g., because
  // the config cannot be parsed.
  static std::unique_ptr<DataProviderImpl> TryCreate(
      async_dispatcher_t* dispatcher,
      std::shared_ptr<::sys::ServiceDirectory> services);

  DataProviderImpl(async_dispatcher_t* dispatcher,
                   std::shared_ptr<::sys::ServiceDirectory> services,
                   const Config& config);

  // |fuchsia.feedback.DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(ImageEncoding encoding,
                     GetScreenshotCallback callback) override;

 private:
  // Connects to Scenic and sets up the error handler in case we lose the
  // connection.
  void ConnectToScenic();

  // Signals to all the pending GetScreenshot callbacks that an error occurred,
  // most likely the loss of the connection with Scenic.
  void TerminateAllGetScreenshotCallbacks();

  async::Executor executor_;
  const std::shared_ptr<::sys::ServiceDirectory> services_;
  const Config config_;

  // TODO(DX-1499): we should have a connection to Scenic per GetScreenshot()
  // call, not a single one overall.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  // We keep track of the pending GetScreenshot callbacks so we can terminate
  // all of them when we lose the connection with Scenic.
  std::vector<std::unique_ptr<GetScreenshotCallback>>
      get_png_screenshot_callbacks_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_
