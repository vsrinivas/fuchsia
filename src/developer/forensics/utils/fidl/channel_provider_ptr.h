// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace fidl {

// Fetches the current update channel.
//
// fuchsia.update.channelcontrol.ChannelControl is expected to be in |services|.
::fpromise::promise<std::string, Error> GetCurrentChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout);

// Fetches the target channel.
//
// fuchsia.update.channelcontrol.ChannelControl is expected to be in |services|.
::fpromise::promise<std::string, Error> GetTargetChannel(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout);

}  // namespace fidl
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIDL_CHANNEL_PROVIDER_PTR_H_
