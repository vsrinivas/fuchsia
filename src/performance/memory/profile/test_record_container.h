// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_TEST_RECORD_CONTAINER_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_TEST_RECORD_CONTAINER_H_

#include <ostream>

#include <trace-test-utils/fixture.h>

#include "src/performance/memory/profile/record_container.h"

// Collecting records from the trace infrastructure fixture.
// Also filters out records collected on other threads and can be printed for debug.
class TestRecordContainer : public RecordContainer {
 public:
  TestRecordContainer();
  bool ForEach(std::function<void(const trace::Record&)> record_consumer) const override;

  const fbl::Vector<trace::Record>& records() const;
  const fbl::Vector<trace::Record>& removed() const;

  bool ReadFromFixture();

 private:
  fbl::Vector<trace::Record> records_;
  fbl::Vector<trace::Record> removed_;
};

std::ostream& operator<<(std::ostream& os, const TestRecordContainer& container);

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_TEST_RECORD_CONTAINER_H_
