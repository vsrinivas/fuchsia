// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_TRACE_RECORDER_IMPL_H_
#define APPS_TRACING_TRACE_RECORDER_IMPL_H_

#include "lib/ftl/macros.h"
#include "mojo/services/tracing/interfaces/tracing.mojom.h"

namespace tracing {

// An Implementation of TraceRecorder sending out incoming
// json parts to a data pipe, inserting ',' as needed to
// produce a stream of events readily consumable by catapult.
class TraceRecorderImpl : public tracing::TraceRecorder {
 public:
  TraceRecorderImpl();
  ~TraceRecorderImpl() override;

  // |TraceRecorder| implementation.
  void Record(const mojo::String& json) override;

  // Sets up |producer_handle| to receive all incoming events.
  void Start(mojo::ScopedDataPipeProducerHandle producer_handle);
  // Resets internal state and closes the internal data pipe producer handle.
  void Stop();

 private:
  bool is_first_entry_ = true;
  mojo::ScopedDataPipeProducerHandle producer_handle_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceRecorderImpl);
};

}  // namespace tracing

#endif  // APPS_TRACING_TRACE_RECORDER_IMPL_H_
