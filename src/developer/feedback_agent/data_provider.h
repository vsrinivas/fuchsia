// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/async_promise/executor.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdint.h>

#include <memory>

#include "src/developer/feedback_agent/config.h"
#include "src/developer/feedback_agent/scenic_ptr.h"

namespace fuchsia {
namespace feedback {

// Provides data useful to attach in feedback reports (crash or user feedback).
class DataProviderImpl : public DataProvider {
 public:
  // Static factory method.
  //
  // Returns nullptr if the data provider cannot be instantiated, e.g., because the config cannot be
  // parsed.
  static std::unique_ptr<DataProviderImpl> TryCreate(
      async_dispatcher_t* dispatcher, std::shared_ptr<::sys::ServiceDirectory> services);

  DataProviderImpl(async_dispatcher_t* dispatcher,
                   std::shared_ptr<::sys::ServiceDirectory> services, const Config& config);

  // |fuchsia.feedback.DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(ImageEncoding encoding, GetScreenshotCallback callback) override;

 private:
  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  const std::shared_ptr<::sys::ServiceDirectory> services_;
  const Config config_;

  uint64_t next_scenic_id_ = 0;
  std::map<uint64_t, std::unique_ptr<Scenic>> scenics_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_DATA_PROVIDER_H_
