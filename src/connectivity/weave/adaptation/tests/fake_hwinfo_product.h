// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_PRODUCT_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_PRODUCT_H_

#include <fuchsia/hwinfo/cpp/fidl_test_base.h>

#include <optional>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.hwinfo.Product.
class FakeHwinfoProduct final : public fuchsia::hwinfo::testing::Product_TestBase {
 public:
  // Default values for fuchsia.hwinfo.Product fields. For brevity, this only
  // includes the fields weavestack processes. No fields in the FIDL table are
  // required to be set.
  static constexpr char kBuildDate[] = "1998-09-04T12:34:56";

  // Convenience variables to compare date information from the above product
  // fields. These fields must be kept aligned with the values above.
  static constexpr uint16_t kBuildDateYear = 1998;
  static constexpr uint8_t kBuildDateMonth = 9;
  static constexpr uint8_t kBuildDateDay = 4;

  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Constructs a FakeHwinfoProduct using the provided configuration values.
  explicit FakeHwinfoProduct(std::optional<std::string> build_date) : build_date_(build_date) {}

  // Constructs a FakeHwinfoProduct using the default configuration values.
  FakeHwinfoProduct() : FakeHwinfoProduct(kBuildDate) {}

  // Returns the current ProductInfo table.
  void GetInfo(fuchsia::hwinfo::Product::GetInfoCallback callback) override {
    fuchsia::hwinfo::ProductInfo product_info;

    if (build_date_) {
      product_info.set_build_date(build_date_.value());
    }

    callback(std::move(product_info));
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Product> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::hwinfo::Product> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Update the build date.
  FakeHwinfoProduct& set_build_date(std::optional<std::string> build_date) {
    build_date_ = build_date;
    return *this;
  }

 private:
  fidl::Binding<fuchsia::hwinfo::Product> binding_{this};
  std::optional<std::string> build_date_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_PRODUCT_H_
