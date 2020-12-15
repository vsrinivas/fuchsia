// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_device_info.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "build_info.h"
#include "harvester.h"
#include "sample_bundle.h"

namespace harvester {

const char kAnnotationBuildBoard[] = "annotations:build.board";
const char kAnnotationBuildProduct[] = "annotations:build.product";
const char kAnnotationDeviceBoardName[] = "annotations:device.board-name";
const char kAnnotationUptime[] = "annotations:uptime";

void GatherDeviceInfo::Gather() {
  SampleBundle samples;
  const auto uptime = zx_clock_get_monotonic();
  samples.AddIntSample(kAnnotationUptime, uptime);
  samples.Upload(DockyardPtr());
}

void ResolveAnnotationValue(BuildInfoValue& annotation,
                            std::string annotation_key,
                            StringSampleList& annotation_values) {
  if (annotation.HasValue()) {
    annotation_values.emplace_back(annotation_key, annotation.Value());
  } else {
    FX_LOGS(ERROR) << annotation_key
                   << " HAS NO VALUE: " << ToString(annotation.Error());
  }
}

void GatherDeviceInfo::GatherDeviceProperties() {
  BuildAnnotations annotations = annotations_provider_->GetAnnotations();

  StringSampleList annotation_values;

  ResolveAnnotationValue(annotations.buildBoard, kAnnotationBuildBoard,
                         annotation_values);
  ResolveAnnotationValue(annotations.buildProduct, kAnnotationBuildProduct,
                         annotation_values);
  ResolveAnnotationValue(annotations.deviceBoardName,
                         kAnnotationDeviceBoardName, annotation_values);

  if (annotation_values.empty()) {
    FX_LOGS(ERROR) << "Failed to gather all desired annotations";
    return;
  }

  DockyardProxyStatus status =
      Dockyard().SendStringSampleList(annotation_values);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendStringSampleList", status)
                   << " The annotation values will be missing";
  }
}

}  // namespace harvester
