// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

namespace forensics {
namespace exceptions {
namespace handler {

struct ComponentInfo {
  std::string url;
  std::string realm_path;
  std::string moniker;
};

// Get component information about the thread with koid |thread_koid|.

// fuchsia.sys.internal.CrashIntrospect and fuchsia.sys2.CrashIntrospect are expected to be in
// |services|.
::fpromise::promise<ComponentInfo> GetComponentInfo(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<sys::ServiceDirectory> services,
                                                    zx::duration timeout, zx_koid_t thread_koid);

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_
