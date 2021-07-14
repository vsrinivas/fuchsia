// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>

#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/promise.h"

namespace forensics {
namespace fidl {
namespace {

// Wraps around fuchsia::update::channelcontrol::ChannelControlPtr to handle establishing the
// connection, losing the connection, waiting for the callback, enforcing a timeout, etc.
//
// Supports only one call to GetCurrentChannel() or GetTargetChannel().
class ChannelProviderPtr {
 public:
  // fuchsia.update.channelcontrol.ChannleControl is expected to be in |services|.
  ChannelProviderPtr(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services)
      : channel_ptr_(dispatcher, std::move(services)) {}

  ::fpromise::promise<std::string, Error> GetCurrentChannel(fit::Timeout timeout) {
    channel_ptr_->GetCurrent([this](std::string channel) {
      if (channel_ptr_.IsAlreadyDone()) {
        return;
      }

      channel_ptr_.CompleteOk(channel);
    });

    return channel_ptr_.WaitForDone(std::move(timeout));
  }

  ::fpromise::promise<std::string, Error> GetTargetChannel(fit::Timeout timeout) {
    channel_ptr_->GetTarget([this](std::string channel) {
      if (channel_ptr_.IsAlreadyDone()) {
        return;
      }

      channel_ptr_.CompleteOk(channel);
    });

    return channel_ptr_.WaitForDone(std::move(timeout));
  }

 private:
  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl, std::string> channel_ptr_;
};

}  // namespace

::fpromise::promise<std::string, Error> GetCurrentChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout) {
  auto ptr = std::make_unique<fidl::ChannelProviderPtr>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = ptr->GetCurrentChannel(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                              /*args=*/std::move(ptr));
}

::fpromise::promise<std::string, Error> GetTargetChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout) {
  auto ptr = std::make_unique<fidl::ChannelProviderPtr>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto channel = ptr->GetTargetChannel(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(channel),
                                              /*args=*/std::move(ptr));
}

}  // namespace fidl
}  // namespace forensics
