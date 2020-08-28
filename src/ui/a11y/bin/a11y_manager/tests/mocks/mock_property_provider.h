// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_PROPERTY_PROVIDER_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_PROPERTY_PROVIDER_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <string>

#include "src/lib/fxl/macros.h"

namespace accessibility_test {

// A Mock to provide locale information to accessibility through the PropertyProvider service.
// By default, calls to GetProfile() are only answered when this mock invokes ReplyToGetProfile().
// See |delay_response_| documentation.
class MockPropertyProvider : public fuchsia::intl::PropertyProvider {
 public:
  explicit MockPropertyProvider(sys::testing::ComponentContextProvider* context);
  ~MockPropertyProvider() = default;

  // Sets a new locale to the user profile. Please see fuchsia.intl.LocaleId for documentation.
  void SetLocale(std::string locale_id);

  // |fuchsia.intl.PropertyProvider|
  void SendOnChangeEvent();

  int get_profile_count() const { return get_profile_count_; }

  // If |delay_response_| is true, Invokes the callback passed in GetProfile(). This is used to
  // simulate different timings for responses. Honnors only the last call to GetProfile().
  void ReplyToGetProfile();

  bool delay_response() const { return delay_response_; }
  void set_delay_response(bool delay_response) { delay_response_ = delay_response; }

  // Close all FIDL clients, by closing their channels.
  void CloseChannels() { property_provider_bindings_.CloseAll(); }

 private:
  // |fuchsia.intl.PropertyProvider|
  void GetProfile(GetProfileCallback callback) override;

  fidl::BindingSet<fuchsia::intl::PropertyProvider> property_provider_bindings_;

  // Permanent locale stored by this mock.
  fuchsia::intl::Profile profile_;
  // Number of times GetProfile() was called.
  int get_profile_count_ = 0;
  // If true, calls to GetProfile() will store the callback, and only invoke it on calls to
  // ReplyToGetProfile().
  bool delay_response_ = true;
  fit::function<void()> callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockPropertyProvider);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_TESTS_MOCKS_MOCK_PROPERTY_PROVIDER_H_
