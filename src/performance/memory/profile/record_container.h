// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_MEMORY_PROFILE_RECORD_CONTAINER_H_
#define SRC_PERFORMANCE_MEMORY_PROFILE_RECORD_CONTAINER_H_

#include <trace-reader/records.h>

// Holds and process trace records.
// Interface introduced for test purpose.
class RecordContainer {
 public:
  // Returns false when the container could not be accessed.
  virtual bool ForEach(std::function<void(const trace::Record&)> record_consumer) const = 0;
};

#endif  // SRC_PERFORMANCE_MEMORY_PROFILE_RECORD_CONTAINER_H_
