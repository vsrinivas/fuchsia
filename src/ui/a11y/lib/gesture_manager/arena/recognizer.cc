// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

GestureRecognizer::~GestureRecognizer() = default;

void GestureRecognizer::AddArenaMember(ArenaMember* arena_member) {
  FX_DCHECK(arena_member);
  arena_member_ = arena_member;
}

void GestureRecognizer::OnWin() {}
void GestureRecognizer::OnDefeat() {}
void GestureRecognizer::OnContestStarted() {}

}  // namespace a11y
