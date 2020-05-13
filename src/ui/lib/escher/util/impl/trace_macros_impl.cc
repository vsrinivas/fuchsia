// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/impl/trace_macros_impl.h"

#include <lib/syslog/cpp/macros.h>

namespace escher {
namespace impl {

void TraceEndOnScopeClose::Initialize(const char* category, const char* name) {
  FX_DCHECK(category && name);
  category_ = category;
  name_ = name;
}

TraceEndOnScopeClose::~TraceEndOnScopeClose() {
  if (category_) {
    AddTraceEvent(TRACE_EVENT_PHASE_END, category_, name_);
  }
}

}  // namespace impl
}  // namespace escher
