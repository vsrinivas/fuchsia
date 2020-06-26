// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_property_provider.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

MockPropertyProvider::MockPropertyProvider(sys::testing::ComponentContextProvider* context) {
  context->service_directory_provider()->AddService(property_provider_bindings_.GetHandler(this));
}

void MockPropertyProvider::GetProfile(GetProfileCallback callback) {
  get_profile_count_++;
  fuchsia::intl::Profile profile;
  if (delay_response_) {
    callback_ = [this, profile = std::move(profile), callback = std::move(callback)]() mutable {
      profile_.Clone(&profile);
      callback(std::move(profile));
    };
  } else {
    profile_.Clone(&profile);
    callback(std::move(profile));
  }
}

void MockPropertyProvider::ReplyToGetProfile() {
  if (delay_response_) {
    if (callback_) {
      callback_();
    }
  }
}

void MockPropertyProvider::SetLocale(std::string locale_id) {
  profile_.mutable_locales()->clear();
  fuchsia::intl::LocaleId id;
  id.id = locale_id;
  profile_.mutable_locales()->emplace_back(std::move(id));
}

void MockPropertyProvider::SendOnChangeEvent() {
  for (auto& binding : property_provider_bindings_.bindings()) {
    binding->events().OnChange();
  }
}

}  // namespace accessibility_test
