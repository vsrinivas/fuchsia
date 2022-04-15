// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/board_info_provider.h"

#include "fuchsia/hwinfo/cpp/fidl.h"
#include "src/developer/forensics/feedback/annotations/constants.h"

namespace forensics::feedback {

Annotations BoardInfoToAnnotations::operator()(const fuchsia::hwinfo::BoardInfo& info) {
  Annotations annotations{
      {kHardwareBoardNameKey, Error::kMissingValue},
      {kHardwareBoardRevisionKey, Error::kMissingValue},
  };

  if (info.has_name()) {
    annotations.insert_or_assign(kHardwareBoardNameKey, info.name());
  }

  if (info.has_revision()) {
    annotations.insert_or_assign(kHardwareBoardRevisionKey, info.revision());
  }

  return annotations;
}

std::set<std::string> BoardInfoProvider::GetKeys() const {
  return {
      kHardwareBoardNameKey,
      kHardwareBoardRevisionKey,
  };
}

}  // namespace forensics::feedback
