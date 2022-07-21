// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotation_providers.h"

#include <memory>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/constants.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/timekeeper/system_clock.h"

namespace forensics::feedback {

AnnotationProviders::AnnotationProviders(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    std::set<std::string> allowlist, Annotations static_annotations,
    std::unique_ptr<CachedAsyncAnnotationProvider> device_id_provider)
    : dispatcher_(dispatcher),
      data_register_(kMaxNumNonPlatformAnnotations, kReservedAnnotationNamespaces,
                     kDataRegisterPath),
      time_provider_(dispatcher, zx::unowned_clock(zx_utc_reference_get()),
                     std::make_unique<timekeeper::SystemClock>()),
      board_info_provider_(dispatcher_, services, AnnotationProviderBackoff()),
      product_info_provider_(dispatcher_, services, AnnotationProviderBackoff()),
      current_channel_provider_(dispatcher_, services, AnnotationProviderBackoff()),
      timezone_provider_(dispatcher_, services, AnnotationProviderBackoff()),
      device_id_provider_(std::move(device_id_provider)),
      target_channel_provider_(dispatcher_, services, AnnotationProviderBackoff()),
      ui_state_provider_(dispatcher_, services, std::make_unique<timekeeper::SystemClock>(),
                         AnnotationProviderBackoff()),
      annotation_manager_(
          dispatcher_, allowlist, static_annotations, &data_register_,
          {&time_provider_, &ui_state_provider_},
          {&board_info_provider_, &product_info_provider_, &current_channel_provider_},
          {&timezone_provider_, device_id_provider_.get(), &ui_state_provider_},
          {&target_channel_provider_}) {
  FX_CHECK(allowlist.size() <= kMaxNumPlatformAnnotations)
      << "Requesting " << allowlist.size() << " annotations when " << kMaxNumPlatformAnnotations
      << " are alloted for the platform";

  if (allowlist.empty()) {
    FX_LOGS(WARNING) << "Annotation allowlist is empty, no platform annotations will be collected";
  }
}

void AnnotationProviders::Handle(
    ::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request,
    ::fit::function<void(zx_status_t)> error_handler) {
  data_register_connections_.AddBinding(&data_register_, std::move(request), dispatcher_,
                                        std::move(error_handler));
}

std::unique_ptr<backoff::Backoff> AnnotationProviders::AnnotationProviderBackoff() {
  return std::unique_ptr<backoff::Backoff>(
      new backoff::ExponentialBackoff(zx::min(1), 2u, zx::hour(1)));
}

}  // namespace forensics::feedback
