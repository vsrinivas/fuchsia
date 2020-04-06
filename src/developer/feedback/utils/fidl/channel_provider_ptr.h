// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_

#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include "src/developer/feedback/utils/bridge.h"
#include "src/lib/fxl/macros.h"

namespace feedback {
namespace fidl {

// Wraps around fuchsia::update::channel::ProviderPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
//
// Supports only one call to GetCurrentChannel().
class ChannelProviderPtr {
 public:
  // fuchsia.update.channel.Provider is expected to be in |services|.
  ChannelProviderPtr(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<std::string> GetCurrentChannel(
      zx::duration timeout, fit::closure if_timeout = [] {});

 private:
  const std::shared_ptr<sys::ServiceDirectory> services_;

  // Enforces the one-shot nature of GetCurrentChannel().
  bool has_called_get_current_channel_ = false;

  fuchsia::update::channel::ProviderPtr connection_;
  Bridge<std::string> pending_call_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelProviderPtr);
};

}  // namespace fidl
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
