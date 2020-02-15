// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
#define SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/intl/merge/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/result.h>

#include <queue>

#include <sdk/lib/sys/cpp/component_context.h>

namespace modular {

// Implementation of `fuchsia.intl.PropertyProvider`.
//
// Serves an up-to-date `fuchsia.intl.Profile`, based on watched user settings.
//
// Example use, with types and required includes elided for brevity is below.
//
// ```
// async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
// auto context = sys::ComponentContext::Create();
// // Connects to required backend services through context->svc().
// auto intl = IntlPropertyProviderImpl::Create(context->svc());
// // Starts serving `fuchsia.intl.PropertyProvider`.
// context->outgoing()->AddPublicService(intl->GetHandler());
// // Waits for events in the async loop.
// loop.Run();
// ```
//
class IntlPropertyProviderImpl final : fuchsia::intl::PropertyProvider {
 public:
  explicit IntlPropertyProviderImpl(fuchsia::settings::IntlPtr settings_client_);

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

 private:
  // Start watching changes in user preferences.  Each setting type is watched separately.
  void StartSettingsWatcher();

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
  bool UpdateRawData(fuchsia::intl::merge::Data new_raw_data);

  // Send the Profile to any queued callers of `GetProfile`.
  void ProcessProfileRequests();

  // A snapshot of the assembled intl `Profile`.
  std::optional<fuchsia::intl::Profile> intl_profile_;

  // Raw data that will be used to assemble the `Profile`.  Initially empty, and remains empty
  // until a first successful read result comes in.
  std::optional<fuchsia::intl::merge::Data> raw_profile_data_;

  fidl::BindingSet<fuchsia::intl::PropertyProvider> property_provider_bindings_;

  // The client connecting to the intl service.
  fuchsia::settings::IntlPtr settings_client_;

  // Queue of pending requests
  std::queue<fuchsia::intl::PropertyProvider::GetProfileCallback> get_profile_queue_;
};

}  // namespace modular

#endif  // SRC_LIB_INTL_INTL_PROPERTY_PROVIDER_IMPL_INTL_PROPERTY_PROVIDER_IMPL_H_
