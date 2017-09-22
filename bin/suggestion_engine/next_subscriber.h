// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/windowed_subscriber.h"

namespace maxwell {

// Manages a single Next suggestion subscriber.
typedef BoundWindowedSuggestionSubscriber<NextController> NextSubscriber;

}  // namespace maxwell
