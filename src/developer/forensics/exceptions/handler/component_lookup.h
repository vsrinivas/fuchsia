// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace exceptions {
namespace handler {

// Get component information about the thread with koid |thread_koid|.
//
// fuchsia.sys.internal.CrashIntrospect is expected to be in |services|.
::fit::promise<fuchsia::sys::internal::SourceIdentity> GetComponentSourceIdentity(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout, zx_koid_t thread_koid);

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_COMPONENT_LOOKUP_H_
