// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/lib/fxl/macros.h"

namespace fuchsia {
namespace crash {

// Wraps around fuchsia::feedback::DataProviderPtr to handle establishing the
// connection, losing the connection, waiting for the callback, etc.
class FeedbackDataProvider {
 public:
  explicit FeedbackDataProvider(
      std::shared_ptr<::sys::ServiceDirectory> services);

  fit::promise<fuchsia::feedback::Data> GetData();

 private:
  const std::shared_ptr<::sys::ServiceDirectory> services_;
  fuchsia::feedback::DataProviderPtr data_provider_;
  fit::bridge<fuchsia::feedback::Data> done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackDataProvider);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
