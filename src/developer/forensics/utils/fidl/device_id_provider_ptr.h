// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DEVICE_ID_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DEVICE_ID_PROVIDER_PTR_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/hanging_get_ptr.h"

namespace forensics {
namespace fidl {

// Wraps around fuchsia::feedback::DeviceIdProviderPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
class DeviceIdProviderPtr {
 public:
  DeviceIdProviderPtr(async_dispatcher_t* dispatcher,
                      std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<std::string, Error> GetId(zx::duration timeout);

 private:
  // Makes the unique call on |connection_|.
  void MakeCall();

  HangingGetPtr<fuchsia::feedback::DeviceIdProvider, std::string> connection_;
};

}  // namespace fidl
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DEVICE_ID_PROVIDER_PTR_H_
