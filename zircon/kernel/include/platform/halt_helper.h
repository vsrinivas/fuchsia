// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <platform.h>

// Gracefully halt and perform |action|.
void platform_graceful_halt_helper(platform_halt_action action);
