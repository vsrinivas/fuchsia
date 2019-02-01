// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"

namespace tracing {

std::ostream& operator<<(std::ostream& out, const TraceProviderBundle& bundle) {
  // The pid and name should be present, so we don't try to get fancy with
  // the formatting if it turns out they're not.
  return out << "#" << bundle.id << " {" << bundle.pid << ":" << bundle.name << "}";
}

}  // namespace tracing
