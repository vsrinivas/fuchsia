// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/channel_provider_ptr.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/feedback/utils/errors.h"
#include "src/developer/feedback/utils/fit/promise.h"

namespace forensics {
namespace fidl {

::fit::promise<std::string, Error> GetCurrentChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout) {
  auto ptr = std::make_unique<fidl::ChannelProviderPtr>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = ptr->GetCurrentChannel(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                              /*args=*/std::move(ptr));
}

ChannelProviderPtr::ChannelProviderPtr(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<sys::ServiceDirectory> services)
    : channel_ptr_(dispatcher, services) {}

::fit::promise<std::string, Error> ChannelProviderPtr::GetCurrentChannel(fit::Timeout timeout) {
  channel_ptr_->GetCurrent([this](std::string channel) {
    if (channel_ptr_.IsAlreadyDone()) {
      return;
    }

    channel_ptr_.CompleteOk(channel);
  });

  return channel_ptr_.WaitForDone(std::move(timeout));
}

}  // namespace fidl
}  // namespace forensics
