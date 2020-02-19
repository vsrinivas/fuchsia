// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace feedback {

// Wraps around fuchsia::feedback::DataProviderPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
class FeedbackDataProvider {
 public:
  FeedbackDataProvider(async_dispatcher_t* dispatcher,
                       std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<fuchsia::feedback::Data> GetData(zx::duration timeout);

 private:
  class WeakBridge {
   public:
    WeakBridge() : weak_ptr_factory_(&bridge_) {}
    fxl::WeakPtr<fit::bridge<fuchsia::feedback::Data>> GetWeakPtr() {
      return weak_ptr_factory_.GetWeakPtr();
    }

   private:
    fit::bridge<fuchsia::feedback::Data> bridge_;
    fxl::WeakPtrFactory<fit::bridge<fuchsia::feedback::Data>> weak_ptr_factory_;
  };

  void ConnectToDataProvider();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::feedback::DataProviderPtr data_provider_;

  // We wrap each fit::bridge in a weak pointer because the fit::bridge can be invalidated through
  // several flows, including a delayed task on the dispatcher, which outlives this class.
  std::map<uint64_t, WeakBridge> pending_get_data_;
  uint64_t next_get_data_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackDataProvider);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_FEEDBACK_DATA_PROVIDER_H_
