// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_MEMBER_CONNECTOR_H_
#define LIB_FIDL_CPP_MEMBER_CONNECTOR_H_

#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/service_connector.h>

namespace fidl {

// A connector for a member of a service instance.
template <typename Protocol>
class MemberConnector final {
 public:
  // Constructs a connector for a member of a service instance.
  //
  // As |dir| is not owned by the connector, it must outlive it.
  MemberConnector(const ServiceConnector* service, std::string name)
      : service_(service), name_(std::move(name)) {}

  // Connects to the member using |request|.
  zx_status_t Connect(InterfaceRequest<Protocol> request) const {
    return service_->Connect(name_, request.TakeChannel());
  }

  // Connects to the member and returns a new handle.
  InterfaceHandle<Protocol> Connect() const {
    InterfaceHandle<Protocol> handle;
    zx_status_t status = service_->Connect(name_, handle.NewRequest().TakeChannel());
    if (status != ZX_OK) {
      return nullptr;
    }
    return handle;
  }

  // Returns the name of this member.
  const std::string& name() const { return name_; }

 private:
  const ServiceConnector* const service_;
  const std::string name_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_MEMBER_CONNECTOR_H_
