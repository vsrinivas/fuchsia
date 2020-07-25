// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_COMPONENT_LOOKUP_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_COMPONENT_LOOKUP_H_

#include <fuchsia/sys/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace exceptions {

// Get component information about the process with koid |process_koid|.
//
// fuchsia.sys.internal.CrashIntrospect is expected to be in |services|.
::fit::promise<fuchsia::sys::internal::SourceIdentity> GetComponentSourceIdentity(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    fit::Timeout timeout, zx_koid_t process_koid);

// Wraps around fuchsia::sys::internal::CrashIntrospectPtr to handle establishing the connection,
// losing the connection, waiting for the callback, enforcing a timeout, etc.
//
// GetSourceIdentity() is expected to be called only once.
class ComponentLookup {
 public:
  // fuchsia.sys.internal.CrashIntrospect is expected to be in |services|.
  ComponentLookup(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fit::promise<fuchsia::sys::internal::SourceIdentity> GetSourceIdentity(zx_koid_t process_koid,
                                                                           fit::Timeout timeout);

 private:
  fidl::OneShotPtr<fuchsia::sys::internal::CrashIntrospect, fuchsia::sys::internal::SourceIdentity>
      introspect_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentLookup);
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_HANDLER_COMPONENT_LOOKUP_H_
