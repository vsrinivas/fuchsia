// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_

#include <functional>
#include <list>
#include <vector>

#include <mx/socket.h>
#include <mx/vmo.h>

#include "apps/tracing/services/trace_provider.fidl.h"
#include "apps/tracing/src/trace_manager/tracee.h"
#include "apps/tracing/src/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/one_shot_timer.h"

namespace tracing {

// TraceSession keeps track of all TraceProvider instances that
// are active for a tracing session.
class TraceSession : public ftl::RefCountedThreadSafe<TraceSession> {
 public:
  // Initializes a new instances that streams results
  // to |destination|. Every provider active in this
  // session is handed |categories| and a vmo of size
  // |trace_buffer_size| when started.
  explicit TraceSession(mx::socket destination,
                        fidl::Array<fidl::String> categories,
                        size_t trace_buffer_size);
  // Frees all allocated resources and closes the outgoing
  // connection.
  ~TraceSession();

  // Starts |provider| and adds it to this session.
  void AddProvider(TraceProviderBundle* provider);
  // Stops |provider|, streaming out all of its trace records.
  void RemoveDeadProvider(TraceProviderBundle* provider);
  // Stops all providers that are part of this session, streams out
  // all remaining trace records and finally invokes |done_callback|.
  //
  // If stopping providers takes longer than |timeout|, we forcefully
  // shutdown operations and invoke |done_callback|.
  void Stop(ftl::Closure done_callback, const ftl::TimeDelta& timeout);

 private:
  void FinishProvider(TraceProviderBundle* bundle);
  void FinishSessionIfEmpty();

  mx::socket destination_;
  fidl::Array<fidl::String> categories_;
  size_t trace_buffer_size_;
  std::vector<uint8_t> buffer_;
  std::list<Tracee> tracees_;
  ftl::OneShotTimer session_finalize_timeout_;
  ftl::Closure done_callback_;

  ftl::WeakPtrFactory<TraceSession> weak_ptr_factory_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TraceSession);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_
