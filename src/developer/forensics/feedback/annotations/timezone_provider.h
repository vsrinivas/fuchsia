// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIMEZONE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIMEZONE_PROVIDER_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <set>
#include <string>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback {

// Caches the most up-to-date version of the system timezone.
//
// fuchsia.intl.PropertyProvider must be in |services|.
class TimezoneProvider : public CachedAsyncAnnotationProvider {
 public:
  TimezoneProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                   std::unique_ptr<backoff::Backoff> backoff);

  std::set<std::string> GetKeys() const override;

  void GetOnUpdate(::fit::function<void(Annotations)> callback) override;

 private:
  void GetTimezone();
  void OnError(zx_status_t status);

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;

  std::optional<std::string> timezone_{std::nullopt};
  fuchsia::intl::PropertyProviderPtr property_provider_ptr_;
  std::unique_ptr<backoff::Backoff> backoff_;

  ::fit::function<void(Annotations)> on_update_;

  fxl::WeakPtrFactory<TimezoneProvider> ptr_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TIMEZONE_PROVIDER_H_
