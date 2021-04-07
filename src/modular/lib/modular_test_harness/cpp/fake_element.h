// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_ELEMENT_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_ELEMENT_H_

#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

class FakeElement : public modular_testing::FakeComponent {
 public:
  explicit FakeElement(modular_testing::FakeComponent::Args args);

  ~FakeElement() override;

  // Instantiates a FakeElement with a randomly generated URL and default sandbox services
  // (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeElement> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) an element expects in its namespace.
  //
  // Default services:
  //  * fuchsia.testing.modular.TestProtocol
  static std::vector<std::string> GetDefaultSandboxServices();

  // Returns a Spec that can be used to propose this element.
  const fuchsia::element::Spec& spec() const { return spec_; }

  // Sets a function to be called when the element's component is created.
  void set_on_create(fit::function<void(fuchsia::sys::StartupInfo)> on_create) {
    on_create_ = std::move(on_create);
  }

  // Sets a function to be called when the element's component is destroyed.
  void set_on_destroy(fit::function<void()> on_destroy) { on_destroy_ = std::move(on_destroy); }

 protected:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |FakeComponent|
  void OnDestroy() override;

 private:
  fuchsia::element::Spec spec_;
  fit::function<void(fuchsia::sys::StartupInfo)> on_create_ =
      [](fuchsia::sys::StartupInfo /*unused*/) {};
  fit::function<void()> on_destroy_ = []() {};
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_ELEMENT_H_
