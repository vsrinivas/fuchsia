// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_
#define LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_

#include <lib/fidl/cpp/wire/service_handler_interface.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace service {

// A handler for an instance of a FIDL Service.
class ServiceHandler final : public fidl::ServiceHandlerInterface {
 public:
  ServiceHandler() = default;

  // Disable copying.
  ServiceHandler(const ServiceHandler&) = delete;
  ServiceHandler& operator=(const ServiceHandler&) = delete;

  // Enable moving.
  ServiceHandler(ServiceHandler&&) = default;
  ServiceHandler& operator=(ServiceHandler&&) = default;

  // Take the underlying pseudo-directory from the service handler.
  //
  // Once taken, the service handler is no longer safe to use.
  fbl::RefPtr<fs::PseudoDir> TakeDirectory() { return std::move(dir_); }

 private:
  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  zx::result<> AddAnyMember(std::string_view member, AnyMemberHandler handler) override {
    // Bridge for fs::Service Callable argument
    auto bridge_func = [handler = std::move(handler)](zx::channel request_channel) {
      handler(fidl::internal::MakeAnyTransport(std::move(request_channel)));
      return ZX_OK;
    };
    return zx::make_result(
        dir_->AddEntry(member, fbl::MakeRefCounted<fs::Service>(std::move(bridge_func))));
  }

  fbl::RefPtr<fs::PseudoDir> dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
};

}  // namespace service

#endif  // LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_
