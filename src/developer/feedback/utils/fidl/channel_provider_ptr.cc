// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/channel_provider_ptr.h"

#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace fidl {

ChannelProviderPtr::ChannelProviderPtr(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<sys::ServiceDirectory> services)
    : services_(services), pending_call_(dispatcher, "Current update channel retrieval") {}

fit::promise<std::string> ChannelProviderPtr::GetCurrentChannel(const zx::duration timeout,
                                                                fit::closure if_timeout) {
  FXL_CHECK(!has_called_get_current_channel_)
      << "GetCurrentChannel() is not intended to be called twice";
  has_called_get_current_channel_ = true;

  connection_ = services_->Connect<fuchsia::update::channel::Provider>();

  connection_.set_error_handler([this](zx_status_t status) {
    if (pending_call_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.update.channel.Provider";
    pending_call_.CompleteError();
  });

  connection_->GetCurrent([this](std::string channel) {
    if (pending_call_.IsAlreadyDone()) {
      return;
    }

    pending_call_.CompleteOk(std::move(channel));
  });

  return pending_call_.WaitForDone(timeout, std::move(if_timeout));
}

}  // namespace fidl
}  // namespace feedback
