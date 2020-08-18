// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_HANDLER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_HANDLER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/exception.h>
#include <lib/zx/time.h>

#include <memory>

namespace forensics {
namespace exceptions {
namespace handler {

// Handles asynchronously filing a crash report for a given zx::exception.
::fit::promise<> Handle(zx::exception exception, async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        zx::duration component_lookup_timeout);

// Handles asynchronously filing a crash report for a given program.
::fit::promise<> Handle(const std::string& process_name, zx_koid_t process_koid,
                        async_dispatcher_t* dispatcher,
                        std::shared_ptr<sys::ServiceDirectory> services,
                        zx::duration component_lookup_timeout);

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_HANDLER_H_
