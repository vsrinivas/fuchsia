// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_BOARD_INFO_PROVIDER_H_

#include <fuchsia/hwinfo/cpp/fidl.h>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

struct BoardInfoToAnnotations {
  Annotations operator()(const fuchsia::hwinfo::BoardInfo& info);
};

// Responsible for collecting annotations for fuchsia.hwinfo/Board.
class BoardInfoProvider
    : public StaticSingleFidlMethodAnnotationProvider<
          fuchsia::hwinfo::Board, &fuchsia::hwinfo::Board::GetInfo, BoardInfoToAnnotations> {
 public:
  using StaticSingleFidlMethodAnnotationProvider::StaticSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
