// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/current_channel_provider.h"

#include "src/developer/forensics/feedback/annotations/constants.h"

namespace forensics::feedback {

Annotations CurrentChannelToAnnotations::operator()(const std::string& current_channel) {
  return Annotations{
      {kSystemUpdateChannelCurrentKey, current_channel},
  };
}

std::set<std::string> CurrentChannelProvider::GetKeys() const {
  return {
      kSystemUpdateChannelCurrentKey,
  };
}

}  // namespace forensics::feedback
