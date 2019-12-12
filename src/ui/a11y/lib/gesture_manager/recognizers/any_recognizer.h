// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ANY_RECOGNIZER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ANY_RECOGNIZER_H_

#include "src/ui/a11y/lib/gesture_manager/arena/contest_member.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

// Recognizer that accepts any gesture. This can be used as a catch-all to make a gesture arena
// consume any gesture not handled by another recognizer.
class AnyRecognizer : public GestureRecognizer {
 public:
  void HandleEvent(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) override;
  void OnContestStarted(std::unique_ptr<ContestMember> contest_member) override;
  std::string DebugName() const override;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_ANY_RECOGNIZER_H_
