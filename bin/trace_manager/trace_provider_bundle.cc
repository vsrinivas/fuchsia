// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"

namespace tracing {

std::ostream& operator<<(std::ostream& out, const TraceProviderBundle& bundle) {
  return out << "#" << bundle.id << " "
             << "'" << bundle.label << "'";
}

}  // namespace tracing
