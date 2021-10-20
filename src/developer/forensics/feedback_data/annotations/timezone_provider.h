// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIMEZONE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIMEZONE_PROVIDER_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/utils/fit/bridge_map.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback_data {

// Fetches the system's primary timezone and returns it as an annotation.
class TimezoneProvider : public AnnotationProvider {
 public:
  // fuchsia.intl/PropertyProvider is expected to be in |services|.
  TimezoneProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  ::fpromise::promise<Annotations> GetAnnotations(zx::duration timeout,
                                                  const AnnotationKeys& allowlist) override;

 private:
  void GetTimezone();
  void OnError(zx_status_t status);

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;

  fit::BridgeMap<std::string> bridges_;
  fuchsia::intl::PropertyProviderPtr property_provider_ptr_;
  std::optional<std::string> timezone_{std::nullopt};
  backoff::ExponentialBackoff backoff_;
  fxl::WeakPtrFactory<TimezoneProvider> weak_ptr_factory_{this};
};

}  // namespace forensics::feedback_data

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_TIMEZONE_PROVIDER_H_
