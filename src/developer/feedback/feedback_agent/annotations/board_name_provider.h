// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_NAME_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_NAME_PROVIDER_H_

#include <optional>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"

namespace feedback {

// Synchronously fetches the name of the device's board.
std::optional<AnnotationValue> GetBoardName();

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_NAME_PROVIDER_H_
