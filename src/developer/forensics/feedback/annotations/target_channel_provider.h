// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TARGET_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TARGET_CHANNEL_PROVIDER_H_

#include <fuchsia/update/channelcontrol/cpp/fidl.h>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

struct TargetChannelToAnnotations {
  Annotations operator()(const ErrorOr<std::string>& target_channel);
};

// Responsible for collecting annotations for fuchsia.hwinfo/Board.
class TargetChannelProvider
    : public DynamicSingleFidlMethodAnnotationProvider<
          fuchsia::update::channelcontrol::ChannelControl,
          &fuchsia::update::channelcontrol::ChannelControl::GetTarget, TargetChannelToAnnotations> {
 public:
  using DynamicSingleFidlMethodAnnotationProvider::DynamicSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_TARGET_CHANNEL_PROVIDER_H_
