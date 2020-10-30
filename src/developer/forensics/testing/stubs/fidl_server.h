// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_FIDL_SERVER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_FIDL_SERVER_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace forensics {
namespace stubs {

// A stub FidlServer that allows a single connection to bind.
template <typename Interface, typename TestBase>
class SingleBindingFidlServer : public TestBase {
 public:
  virtual ::fidl::InterfaceRequestHandler<Interface> GetHandler() {
    return [this](::fidl::InterfaceRequest<Interface> request) {
      binding_ = std::make_unique<::fidl::Binding<Interface>>(this, std::move(request));
    };
  }

  void CloseConnection() {
    if (binding_) {
      binding_->Close(ZX_ERR_PEER_CLOSED);
    }
  }

  bool IsBound() const { return binding_->is_bound(); }

  // |TestBase|
  void NotImplemented_(const std::string& name) override {
    FX_NOTIMPLEMENTED() << name << " is not implemented";
  }

 protected:
  std::unique_ptr<::fidl::Binding<Interface>>& binding() { return binding_; }

 private:
  std::unique_ptr<::fidl::Binding<Interface>> binding_;
};

// A stub FidlServer that allows multiple connections to bind.
template <typename Interface, typename TestBase>
class MultiBindingFidlServer : public TestBase {
 public:
  virtual ::fidl::InterfaceRequestHandler<Interface> GetHandler() {
    return [this](::fidl::InterfaceRequest<Interface> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  void CloseAllConnections() { bindings_.CloseAll(); }

  size_t NumConnections() { bindings_.size(); }

  // |TestBase|
  void NotImplemented_(const std::string& name) override {
    FX_NOTIMPLEMENTED() << name << " is not implemented";
  }

 protected:
  ::fidl::BindingSet<Interface>& bindings() { return bindings_; }

 private:
  ::fidl::BindingSet<Interface> bindings_;
};

}  // namespace stubs
}  // namespace forensics

#define SINGLE_BINDING_STUB_FIDL_SERVER(_1, _2) \
  forensics::stubs::SingleBindingFidlServer<_1::_2, _1::testing::_2##_TestBase>

#define MULTI_BINDING_STUB_FIDL_SERVER(_1, _2) \
  forensics::stubs::MultiBindingFidlServer<_1::_2, _1::testing::_2##_TestBase>

#define STUB_METHOD_DOES_NOT_RETURN(METHOD, PARAM_TYPES...) \
  void METHOD(PARAM_TYPES) override {}

#define STUB_METHOD_CLOSES_CONNECTION(METHOD, PARAM_TYPES...) \
  void METHOD(PARAM_TYPES) override { CloseConnection(); }

#define STUB_METHOD_CLOSES_ALL_CONNECTIONS(METHOD, PARAM_TYPES...) \
  void METHOD(PARAM_TYPES) override { CloseAllConnections(); }

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_FIDL_SERVER_H_
