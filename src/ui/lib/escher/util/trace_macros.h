// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_TRACE_MACROS_H_
#define SRC_UI_LIB_ESCHER_UTIL_TRACE_MACROS_H_

// See <trace/event.h> for usage documentation.

#include "src/lib/fxl/build_config.h"

#ifdef OS_FUCHSIA
#include <trace/event.h>

#include "trace-vthread/event_vthread.h"
#else
#include "src/ui/lib/escher/util/impl/trace_macros_impl.h"

#define TRACE_NONCE() 0

#define TRACE_DURATION(category_literal, name_literal, args...) \
  TRACE_INTERNAL_DURATION((category_literal), (name_literal), args)
#endif

#endif  // SRC_UI_LIB_ESCHER_UTIL_TRACE_MACROS_H_
