// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"

namespace forensics {
namespace feedback_data {

// Manages the lifetime of the Feedback device Id.
//
// // While the protocol is a hanging get, the feedback ID does not change in its implementation so
// each server can just return the ID on the first call of each connection.
class DeviceIdManager {
 public:
  DeviceIdManager(async_dispatcher_t* dispatcher, const std::string& path);

  void AddBinding(::fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request,
                  ::fit::function<void(zx_status_t)> on_channel_close);

 private:
  async_dispatcher_t* dispatcher_;
  std::string device_id_;

  size_t next_provider_idx_;
  std::map<size_t, std::unique_ptr<fuchsia::feedback::DeviceIdProvider>> providers_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DEVICE_ID_PROVIDER_H_
