// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_INSPECT_H_
#define PERIDOT_BIN_LEDGER_TESTING_INSPECT_H_

#include <fuchsia/inspect/cpp/fidl.h>

namespace ledger {

// TODO(crjohns, nathaniel): Move to an "Inspect API testing helpers" library,
// parameterizing by the metric name rather than having "requests" hard-coded.
// EXPECTs that |object| has a metric named "requests" and that the value of
// the "requests" metric is |expected_value|.
void ExpectRequestsMetric(fuchsia::inspect::Object* object,
                          unsigned long expected_value);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_INSPECT_H_
