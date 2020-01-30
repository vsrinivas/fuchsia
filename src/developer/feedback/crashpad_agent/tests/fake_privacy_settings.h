// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_FAKE_PRIVACY_SETTINGS_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_FAKE_PRIVACY_SETTINGS_H_

#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <vector>

namespace feedback {

// Fake fuchsia.settings.Privacy service.
//
// The hanging get pattern behind Watch() requires us to maintain a separate handler per connection
// to be able to track each connection. Here, we only make a single connection in the unit tests
// anyway so it's fine if the fake service can have at most one connection.
class FakePrivacySettings : public fuchsia::settings::Privacy {
 public:
  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::settings::Privacy> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::settings::Privacy> request) {
      binding_ =
          std::make_unique<fidl::Binding<fuchsia::settings::Privacy>>(this, std::move(request));
    };
  }

  // |fuchsia::settings::Privacy|
  void Watch(WatchCallback callback) override;
  void Set(fuchsia::settings::PrivacySettings settings, SetCallback callback) override;

 protected:
  void CloseConnection();

 private:
  void NotifyWatcher();

  // We use a Binding (single connection) and not a BindingSet (multiple connections in parallel) as
  // we don't want to have to maintain a separate handler per connection to implement the hanging
  // get pattern.
  std::unique_ptr<fidl::Binding<fuchsia::settings::Privacy>> binding_;
  fuchsia::settings::PrivacySettings settings_;
  bool dirty_bit_ = true;
  std::unique_ptr<WatchCallback> watcher_;
};

class FakePrivacySettingsClosesConnection : public FakePrivacySettings {
 public:
  void Watch(WatchCallback callback) { CloseConnection(); }
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_FAKE_PRIVACY_SETTINGS_H_
