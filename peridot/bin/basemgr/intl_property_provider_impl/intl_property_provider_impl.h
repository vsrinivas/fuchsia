// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
#define PERIDOT_BIN_BASEMGR_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_

#include <fuchsia/deprecatedtimezone/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/intl/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/result.h>
#include <sdk/lib/sys/cpp/component_context.h>

#include <queue>

namespace modular {

// Implementation of `fuchsia.intl.PropertyProvider`.
//
// Serves an up-to-date `fuchsia.intl.Profile`, based on watched user settings.
class IntlPropertyProviderImpl : fuchsia::intl::PropertyProvider,
                                 fuchsia::deprecatedtimezone::TimezoneWatcher {
 public:
  IntlPropertyProviderImpl(fuchsia::deprecatedtimezone::TimezonePtr time_zone_client);

  // Create an instance of `IntlPropertyProviderImpl`, after using the given `ServiceDirectory` to
  // connect to all of the provider's service dependencies.
  static std::unique_ptr<IntlPropertyProviderImpl> Create(
      const std::shared_ptr<sys::ServiceDirectory>& incoming_services);

  fidl::InterfaceRequestHandler<fuchsia::intl::PropertyProvider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr);

  // Start serving the intl profile and listening for user preference changes.
  void Start();

  // Put the callback in a queue (in case the data is not yet available).
  //
  // `fuchsia.intl.PropertyProvider`
  void GetProfile(fuchsia::intl::PropertyProvider::GetProfileCallback callback) override;

  // `fuchsia.deprecatedtimezone.TimezoneWatcher`
  void OnTimezoneOffsetChange(std::string time_zone_id) override;

 private:
  // Load initial ICU data if this hasn't been done already.
  //
  // TODO(kpozin): Eventually, this should solely be the responsibility of the client component that
  // links `IntlPropertyProviderImpl`, which has a better idea of what parameters ICU should be
  // initialized with.
  zx_status_t InitializeIcuIfNeeded();

  // Load the initial profiles values from user preferences and defaults.
  void LoadInitialValues();

  // Start watching user preferences.
  zx_status_t StartSettingsWatchers();

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

  // A snapshot of the assembled intl `Profile`.
  std::optional<fuchsia::intl::Profile> intl_profile_;

  // Raw data that will be used to assemble the `Profile`.
  std::optional<fuchsia::modular::intl::internal::RawProfileData> raw_profile_data_;

  fidl::BindingSet<fuchsia::intl::PropertyProvider> property_provider_bindings_;

  // TODO(MF-168): Add SetUI service client
  fuchsia::deprecatedtimezone::TimezonePtr time_zone_client_;
  fidl::Binding<fuchsia::deprecatedtimezone::TimezoneWatcher> tz_watcher_binding_;

  // Queue of pending requests
  std::queue<fuchsia::intl::PropertyProvider::GetProfileCallback> get_profile_queue_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
