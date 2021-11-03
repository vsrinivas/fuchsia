// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_PRODUCT_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_PRODUCT_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using ProductInfoProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::hwinfo, Product);

// Stub fuchsia.hwinfo.Product service to return controlled response to GetInfo().
class ProductInfoProvider : public ProductInfoProviderBase {
 public:
  ProductInfoProvider(fuchsia::hwinfo::ProductInfo&& info) : info_(std::move(info)) {}

  // |fuchsia::hwinfo::Product|
  void GetInfo(GetInfoCallback callback) override;

 private:
  fuchsia::hwinfo::ProductInfo info_;
  bool has_been_called_ = false;
};

class ProductInfoProviderNeverReturns : public ProductInfoProviderBase {
 public:
  ProductInfoProviderNeverReturns() {}

  // |fuchsia::hwinfo::Product|
  STUB_METHOD_DOES_NOT_RETURN(GetInfo, GetInfoCallback)
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_PRODUCT_INFO_PROVIDER_H_
