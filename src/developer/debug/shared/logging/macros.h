// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

// Simple macros to be used by the debug logging system.

// Join two tokens together.
// Useful for making unique names: STRINGIFY("foo", __LINE__) -> foo54.
#define STRINGIFY_INTERNAL(x, y) x##y
#define STRINGIFY(x, y) STRINGIFY_INTERNAL(x, y)
