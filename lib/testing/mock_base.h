// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_MOCK_BASE_H_
#define PERIDOT_LIB_TESTING_MOCK_BASE_H_

#include <map>
#include <string>

namespace modular {
namespace testing {

// Helper class for unit testing that provides a collection to track the number
// of times each function is called, then provides functions to validate those
// calls.
class MockBase {
 public:
  MockBase();
  virtual ~MockBase();

  // Validates that the given function was called once and removes that
  // information from the call history
  void ExpectCalledOnce(const std::string& func);

  // Removes the entire history of functions called
  void ClearCalls();

  // Validates that the call history is empty
  void ExpectNoOtherCalls();

 protected:
  std::map<std::string, unsigned int> counts;
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_MOCK_BASE_H_
