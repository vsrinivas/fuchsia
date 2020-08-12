// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DATA_PROVIDER_PTR_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DATA_PROVIDER_PTR_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/bridge_map.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace fidl {

// Wraps around fuchsia::feedback::DataProviderPtr to handle establishing the connection, losing the
// connection, waiting for the callback, enforcing a timeout, etc.
//
// Supports multiple calls to GetSnapshot(). Only one connection exists at a time.
class DataProviderPtr {
 public:
  DataProviderPtr(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<fuchsia::feedback::Snapshot, Error> GetSnapshot(zx::duration timeout);

 private:
  void Connect();

  const std::shared_ptr<sys::ServiceDirectory> services_;

  fuchsia::feedback::DataProviderPtr connection_;
  fit::BridgeMap<fuchsia::feedback::Snapshot> pending_calls_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DataProviderPtr);
};

}  // namespace fidl
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIDL_DATA_PROVIDER_PTR_H_
