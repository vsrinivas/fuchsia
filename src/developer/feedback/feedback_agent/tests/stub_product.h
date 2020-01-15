// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_PRODUCT_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_PRODUCT_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <string>

namespace feedback {

// Stub Product service to return controlled response to Product::GetInfo().
class StubProduct : public fuchsia::hwinfo::Product {
 public:
  StubProduct(fuchsia::hwinfo::ProductInfo&& info) : info_(std::move(info)) {}

  // Returns a request handler for a binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Product> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::hwinfo::Product> request) {
      binding_ =
          std::make_unique<fidl::Binding<fuchsia::hwinfo::Product>>(this, std::move(request));
    };
  }

  // |fuchsia.hwinfo.Product|
  void GetInfo(GetInfoCallback callback) override;

 protected:
  void CloseConnection();

 private:
  std::unique_ptr<fidl::Binding<fuchsia::hwinfo::Product>> binding_;
  fuchsia::hwinfo::ProductInfo info_;
  bool has_been_called_ = false;
};

class StubProductNeverReturns : public StubProduct {
 public:
  StubProductNeverReturns() : StubProduct(fuchsia::hwinfo::ProductInfo()) {}

  // |fuchsia.hwinfo.Product|
  void GetInfo(GetInfoCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_PRODUCT_H_
