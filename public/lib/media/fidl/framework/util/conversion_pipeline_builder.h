// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_CONVERSION_PIPELINE_BUILDER_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_CONVERSION_PIPELINE_BUILDER_H_

#include "apps/media/services/framework/graph.h"
#include "apps/media/services/framework/packet.h"
#include "apps/media/services/framework/types/stream_type.h"

namespace mojo {
namespace media {

// Attempts to add transforms to the given pipeline to convert in_type to a
// type compatible with out_type_sets. If it succeeds, returns true, updates
// *output and delivers the resulting output type via *out_type. If it fails,
// returns false, sets *out_type to nullptr and leaves *output unchanged.
bool BuildConversionPipeline(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph,
    OutputRef* output,
    std::unique_ptr<StreamType>* out_type);

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_UTIL_CONVERSION_PIPELINE_BUILDER_H_
