// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/arena_v2/recognizer_v2.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

GestureRecognizerV2::~GestureRecognizerV2() = default;

void GestureRecognizerV2::OnWin() {}
void GestureRecognizerV2::OnDefeat() {}

}  // namespace a11y
