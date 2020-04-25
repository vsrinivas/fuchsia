// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_
#define LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_

#include <lib/fidl/llcpp/service_handler_interface.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>

namespace llcpp::sys {

// A handler for an instance of a FIDL Service.
class ServiceHandler : public ::llcpp::fidl::ServiceHandlerInterface {
 public:
  ServiceHandler() = default;

  // Disable copying.
  ServiceHandler(const ServiceHandler&) = delete;
  ServiceHandler& operator=(const ServiceHandler&) = delete;

  // Enable moving.
  ServiceHandler(ServiceHandler&&) = default;
  ServiceHandler& operator=(ServiceHandler&&) = default;

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  zx_status_t AddMember(fit::string_view member, MemberHandler handler) override {
    // Bridge between fit::function and fbl::Function.
    auto bridge_func = [handler = std::move(handler)](::zx::channel request_channel) {
      return handler(std::move(request_channel));
    };
    return dir_->AddEntry(std::move(member),
                          fbl::MakeRefCounted<fs::Service>(std::move(bridge_func)));
  }

  // Take the underlying pseudo-directory from the service handler.
  //
  // Once taken, the service handler is no longer safe to use.
  fbl::RefPtr<fs::PseudoDir> TakeDirectory() { return std::move(dir_); }

 private:
  fbl::RefPtr<fs::PseudoDir> dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
};

}  // namespace llcpp::sys

#endif  // LIB_SERVICE_LLCPP_SERVICE_HANDLER_H_
