// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/build_config.h"

#ifdef OS_FUCHSIA
#include <trace/event.h>
#else
// No-op placeholders.
#define TRACE_DURATION(category, name, args...)
#endif
