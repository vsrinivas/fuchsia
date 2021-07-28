// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include <vector>

#include "src/developer/forensics/feedback/device_id_provider.h"
#include "src/developer/forensics/feedback_data/annotations/annotation_provider.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics {
namespace feedback_data {

// Get the annotations providers that can  be used safely to collect annotations multiple times,
// this includes providers that are caching asynchronous static annotations as well as providers
// offering dynamic annotations that don't require connecting to a service.
std::vector<std::unique_ptr<AnnotationProvider>> GetReusableProviders(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    feedback::DeviceIdProvider* device_id_provider, cobalt::Logger* cobalt);

// Get the annotations providers that can only be used once to collect annotations, typically
// providers that have a one-shot connection to a service.
std::vector<std::unique_ptr<AnnotationProvider>> GetSingleUseProviders(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    cobalt::Logger* cobalt);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ANNOTATIONS_ANNOTATION_PROVIDER_FACTORY_H_
