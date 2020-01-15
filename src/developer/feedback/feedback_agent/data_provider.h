// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
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

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const Config config_;
  RefCountedDelayedTask after_timeout_;
  async::Executor executor_;
  std::shared_ptr<Cobalt> cobalt_;

  // We run the Inspect data collection in a separate loop, thread and executor as the calling
  // component will itself be discovered and we don't want to deadlock it, cf. fxb/4632.
  //
  // Ideally, DataProvider wouldn't have to own the loop and executor, but the lifetime of the
  // executor needs to be guaranteed as long as it has tasks scheduled and the current task could be
  // hanging.
  //
  // Note that the second thread could be left dangling if it hangs forever trying to opendir() a
  // currently serving out/ directory from one of the discovered components. It is okay to have
  // potentially dangling threads as we run each fuchsia.feedback.DataProvider request in a separate
  // process that exits when the connection with the client is closed.
  async::Loop inspect_loop_;
  async::Executor inspect_executor_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DATA_PROVIDER_H_
