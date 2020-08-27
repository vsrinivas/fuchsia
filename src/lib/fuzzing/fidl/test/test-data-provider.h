// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_TEST_DATA_PROVIDER_H_
#define SRC_LIB_FUZZING_FIDL_TEST_TEST_DATA_PROVIDER_H_

#include <string>

#include "data-provider.h"

namespace fuzzing {

// Test class that exposes protected methods for testing.
class TestDataProvider : public DataProviderImpl {
 public:
  bool HasLabel(const std::string &label) { return DataProviderImpl::HasLabel(label); }
  bool IsMapped(const std::string &label) { return DataProviderImpl::IsMapped(label); }
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_TEST_DATA_PROVIDER_H_
