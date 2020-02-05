// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
#define SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/intl/internal/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/result.h>

#include <queue>

#include <sdk/lib/sys/cpp/component_context.h>

namespace modular {

// Implementation of `fuchsia.intl.PropertyProvider`.
//
// Serves an up-to-date `fuchsia.intl.Profile`, based on watched user settings.
class IntlPropertyProviderImpl : fuchsia::intl::PropertyProvider, fuchsia::setui::SettingListener {
 public:
  explicit IntlPropertyProviderImpl(fuchsia::setui::SetUiServicePtr setui_client);

  // Create an instance of `IntlPropertyProviderImpl`, after using the given `ServiceDirectory` to
  // connect to all of the provider's service dependencies.
  static std::unique_ptr<IntlPropertyProviderImpl> Create(
      const std::shared_ptr<sys::ServiceDirectory>& incoming_services);

  // Returns the client-side handler for `fuchsia.intl.PropertyProvider`, based on either the
  // dispatcher that is passed in (e.g. for testing), or the default thread-local dispatcher.
  fidl::InterfaceRequestHandler<fuchsia::intl::PropertyProvider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr);

  // Start serving the intl profile and listening for user preference changes.
  void Start();

  // Put the callback in a queue (in case the data is not yet available).
  //
  // `fuchsia.intl.PropertyProvider`
  void GetProfile(fuchsia::intl::PropertyProvider::GetProfileCallback callback) override;

  // Called every time the `setui` calls on the listener.  The settings object corresponds to
  // the setting type that is registered with `setui.Listen`.
  //
  // `fuchsia.setui.SettingListener`
  void Notify(fuchsia::setui::SettingsObject settings) override;

 private:
  // Start watching changes in user preferences.  Each setting type is watched separately.
  void StartSettingsWatcher(fuchsia::setui::SettingType type);

  // Load the initial profiles values from user preferences and defaults.
  void LoadInitialValues();

  // Get a clone of the current `Profile` if available. If the raw data has not yet been
  // initialized, returns `ZX_ERR_SHOULD_WAIT`. Other errors are also possible, e.g.
  // `ZX_ERR_INVALID_ARGS` if the raw data is invalid or `ZX_ERR_INTERNAL` if various internal
  // problems arise.
  fit::result<fuchsia::intl::Profile, zx_status_t> GetProfileInternal();

  // Return true if the initial raw data has been set and is ready to be transformed into a
  // `Profile`.
  bool IsRawDataInitialized();

  // Replace the stored raw data, and, if the data has actually changed, trigger notifications to
  // watchers and pending requesters.
  bool UpdateRawData(fuchsia::modular::intl::internal::RawProfileData& new_raw_data);

  // Notify watchers that the `Profile` has changed.
  void NotifyOnChange();

  // Send the Profile to any queued callers of `GetProfile`.
  void ProcessGetProfileQueue();

  // Updates the internal intl object state, const referenced.
  void NotifyInternal(const fuchsia::setui::SettingsObject& settings);

  // A snapshot of the assembled intl `Profile`.
  std::optional<fuchsia::intl::Profile> intl_profile_;

  // Raw data that will be used to assemble the `Profile`.  Initially empty, and remains empty
  // until a first successful read result comes in from `setui`.
  std::optional<fuchsia::modular::intl::internal::RawProfileData> raw_profile_data_;

  fidl::BindingSet<fuchsia::intl::PropertyProvider> property_provider_bindings_;

  fuchsia::setui::SetUiServicePtr setui_client_;

  // A FIDL connection to the `setui.SettingListener` endpoint which will be called by the
  // `setui` server to deliver the `Notify` for a setting.
  fidl::Binding<fuchsia::setui::SettingListener> setting_listener_binding_;
  // We need separate bindings per type.
  fidl::Binding<fuchsia::setui::SettingListener> setting_listener_binding_intl_;

  // Queue of pending requests
  std::queue<fuchsia::intl::PropertyProvider::GetProfileCallback> get_profile_queue_;

  // A setting object to use for initialization, must be live as long as this object lives.
  const fuchsia::setui::SettingsObject initial_settings_object_;
};

}  // namespace modular

#endif  // SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
