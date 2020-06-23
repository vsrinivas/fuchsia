// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_

#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>

#include "src/developer/feedback/utils/fidl/oneshot_ptr.h"
#include "src/developer/feedback/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace fidl {

// Fetches the current update channel.
//
// fuchsia.update.channel.Provider is expected to be in |services|.
::fit::promise<std::string, Error> GetCurrentChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout);

// Wraps around fuchsia::update::channel::ProviderPtr to handle establishing the connection,
// losing the connection, waiting for the callback, enforcing a timeout, etc.
//
// Supports only one call to GetCurrentChannel().
class ChannelProviderPtr {
 public:
  // fuchsia.update.channel.Provider is expected to be in |services|.
  ChannelProviderPtr(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<std::string, Error> GetCurrentChannel(fit::Timeout timeout);

 private:
  OneShotPtr<fuchsia::update::channel::Provider, std::string> channel_ptr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelProviderPtr);
};

}  // namespace fidl
}  // namespace forensics

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
