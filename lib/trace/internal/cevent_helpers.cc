// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/cevent_helpers.h"

#include "apps/tracing/lib/trace/internal/event_helpers.h"

__BEGIN_CDECLS

uint64_t ctrace_internal_nonce() {
  return tracing::internal::TraceNonce();
}

void ctrace_internal_cleanup_duration_scope(ctrace_internal_duration_scope_t* scope) {
  CTRACE_INTERNAL_DURATION_END(scope->category, scope->name, ());
}

__END_CDECLS
