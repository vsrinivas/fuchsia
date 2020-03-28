// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/feedback/utils/bridge_map.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Wraps around fuchsia::feedback::DataProviderPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
class FeedbackDataProvider {
 public:
  FeedbackDataProvider(async_dispatcher_t* dispatcher,
                       std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<fuchsia::feedback::Data> GetData(zx::duration timeout);

 private:
  void ConnectToDataProvider();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::feedback::DataProviderPtr data_provider_;

  BridgeMap<fuchsia::feedback::Data> pending_get_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackDataProvider);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
