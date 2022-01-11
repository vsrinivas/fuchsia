// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/timezone_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics::feedback_data {
namespace {

static const AnnotationKeys* kSupportedAnnotations = new AnnotationKeys({
    kAnnotationSystemTimezonePrimary,
});

}  // namespace

TimezoneProvider::TimezoneProvider(async_dispatcher_t* dispatcher,
                                   std::shared_ptr<sys::ServiceDirectory> services)
    : dispatcher_(dispatcher),
      services_(services),
      bridges_(dispatcher_),
      backoff_(/*initial_delay=*/zx::sec(10), /*retry_factor=*/2u,
               /*max_delay=*/zx::hour(1)) {
  services_->Connect(property_provider_ptr_.NewRequest(dispatcher_));
  property_provider_ptr_.events().OnChange =
      ::fit::bind_member<&TimezoneProvider::GetTimezone>(this);
  property_provider_ptr_.set_error_handler(::fit::bind_member<&TimezoneProvider::OnError>(this));

  GetTimezone();
}

::fpromise::promise<Annotations> TimezoneProvider::GetAnnotations(zx::duration timeout,
                                                                  const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, *kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({}));
  }

  if (timezone_.has_value()) {
    return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({
        {kAnnotationSystemTimezonePrimary, *timezone_},
    }));
  }

  const uint64_t id = bridges_.NewBridgeForTask("GetTimezone");
  return bridges_.WaitForDone(id, fit::Timeout(timeout))
      .then([this, id](const ::fpromise::result<std::string, Error>& result) {
        AnnotationOr annotation(Error::kNotSet);
        if (result.is_ok()) {
          annotation = result.value();
        } else {
          annotation = result.error();
        }

        bridges_.Delete(id);
        return ::fpromise::make_result_promise<Annotations>(::fpromise::ok<Annotations>({
            {kAnnotationSystemTimezonePrimary, std::move(annotation)},
        }));
      });
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
    bridges_.CompleteAllOk(*timezone_);
  });
}

void TimezoneProvider::OnError(const zx_status_t status) {
  bridges_.CompleteAllError(Error::kConnectionError);
  async::PostDelayedTask(
      dispatcher_,
      [weak_ptr = weak_ptr_factory_.GetWeakPtr()]() {
        if (!weak_ptr) {
          return;
        }

        auto& thiz = *weak_ptr;
        thiz.services_->Connect(thiz.property_provider_ptr_.NewRequest(thiz.dispatcher_));
        thiz.GetTimezone();
        thiz.backoff_.Reset();
      },
      backoff_.GetNext());
}

}  // namespace forensics::feedback_data
