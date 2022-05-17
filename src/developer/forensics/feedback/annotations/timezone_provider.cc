// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/timezone_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <memory>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/lib/backoff/backoff.h"

namespace forensics::feedback {

TimezoneProvider::TimezoneProvider(async_dispatcher_t* dispatcher,
                                   std::shared_ptr<sys::ServiceDirectory> services,
                                   std::unique_ptr<backoff::Backoff> backoff)
    : dispatcher_(dispatcher), services_(services), backoff_(std::move(backoff)) {
  services_->Connect(property_provider_ptr_.NewRequest(dispatcher_));
  property_provider_ptr_.events().OnChange =
      ::fit::bind_member<&TimezoneProvider::GetTimezone>(this);

  property_provider_ptr_.set_error_handler([this](const zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.intl.PropertyProvider";

    auto self = ptr_factory_.GetWeakPtr();
    async::PostDelayedTask(
        dispatcher_,
        [self] {
          if (self) {
            self->services_->Connect(self->property_provider_ptr_.NewRequest(self->dispatcher_));
            self->GetTimezone();
          }
        },
        backoff_->GetNext());
  });

  GetTimezone();
}

std::set<std::string> TimezoneProvider::GetKeys() const {
  return {
      kSystemTimezonePrimaryKey,
  };
}

void TimezoneProvider::GetOnUpdate(::fit::function<void(Annotations)> callback) {
  on_update_ = std::move(callback);

  if (timezone_.has_value()) {
    on_update_({
        {kSystemTimezonePrimaryKey, *timezone_},
    });
  }
}

void TimezoneProvider::GetTimezone() {
  FX_CHECK(property_provider_ptr_.is_bound());

  property_provider_ptr_->GetProfile([this](const fuchsia::intl::Profile profile) {
    if (!profile.has_time_zones()) {
      return;
    }

    const auto& time_zones = profile.time_zones();
    if (time_zones.empty()) {
      return;
    }

    timezone_ = time_zones.front().id;
    on_update_({
        {kSystemTimezonePrimaryKey, *timezone_},
    });
  });
}

}  // namespace forensics::feedback
