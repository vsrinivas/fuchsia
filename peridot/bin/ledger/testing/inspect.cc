// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/inspect.h"

#include <fuchsia/inspect/cpp/fidl.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"

namespace ledger {

// TODO(crjohns, nathaniel): Migrate this to a testing::Matcher.
void ExpectRequestsMetric(fuchsia::inspect::Object* object,
                          unsigned long expected_value) {
  bool requests_found = false;
  unsigned long extra_requests_found = 0UL;
  unsigned long requests = 0UL;
  for (auto& index : *object->metrics) {
    if (index.key == kRequestsInspectPathComponent) {
      if (!requests_found) {
        requests_found = true;
        requests = index.value.uint_value();
      } else {
        extra_requests_found++;
      }
    }
  }
  EXPECT_TRUE(requests_found);
  EXPECT_EQ(expected_value, requests);
  EXPECT_EQ(0UL, extra_requests_found);
}

}  // namespace ledger
