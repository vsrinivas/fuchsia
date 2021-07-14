// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/channel_provider.h"

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"

namespace forensics {
namespace feedback_data {
namespace {

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationSystemUpdateChannelCurrent,
    kAnnotationSystemUpdateChannelTarget,
};

using AnnotationPair = std::pair<std::string, AnnotationOr>;

AnnotationPair MakeAnnotationPair(const std::string& key,
                                  const ::fpromise::result<std::string, Error>& result) {
  AnnotationOr annotation =
      (result.is_ok()) ? AnnotationOr(result.value()) : AnnotationOr(result.error());

  return std::make_pair(key, std::move(annotation));
}

}  // namespace

ChannelProvider::ChannelProvider(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 cobalt::Logger* cobalt)
    : dispatcher_(dispatcher), services_(services), cobalt_(cobalt) {}

::fpromise::promise<Annotations> ChannelProvider::GetAnnotations(zx::duration timeout,
                                                                 const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);

  std::vector<::fpromise::promise<AnnotationPair>> annotation_promises;
  if (annotations_to_get.find(kAnnotationSystemUpdateChannelCurrent) != annotations_to_get.end()) {
    annotation_promises.push_back(
        fidl::GetCurrentChannel(dispatcher_, services_, fit::Timeout(timeout))
            .then([](const ::fpromise::result<std::string, Error>& result) {
              return ::fpromise::ok(
                  MakeAnnotationPair(kAnnotationSystemUpdateChannelCurrent, result));
            }));
  }

  if (annotations_to_get.find(kAnnotationSystemUpdateChannelTarget) != annotations_to_get.end()) {
    annotation_promises.push_back(
        fidl::GetTargetChannel(dispatcher_, services_, fit::Timeout(timeout))
            .then([](const ::fpromise::result<std::string, Error>& result) {
              return ::fpromise::ok(
                  MakeAnnotationPair(kAnnotationSystemUpdateChannelTarget, result));
            }));
  }

  return ::fpromise::join_promise_vector(std::move(annotation_promises))
      .and_then([cobalt = cobalt_](std::vector<::fpromise::result<AnnotationPair>>& results)
                    -> ::fpromise::result<Annotations> {
        Annotations annotations;
        for (auto& result : results) {
          annotations.insert(std::move(result).value());
        }

        bool found_timeout{false};
        for (const auto& [_, v] : annotations) {
          found_timeout |= (!v.HasValue() && v.Error() == Error::kTimeout);
        }

        if (found_timeout) {
          cobalt->LogOccurrence(cobalt::TimedOutData::kChannel);
        }

        return ::fpromise::ok(std::move(annotations));
      });
}

}  // namespace feedback_data
}  // namespace forensics
