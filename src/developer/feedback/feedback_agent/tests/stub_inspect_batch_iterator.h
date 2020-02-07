// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_BATCH_ITERATOR_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_BATCH_ITERATOR_H_

#include <fuchsia/diagnostics/cpp/fidl.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace feedback {

// Stub Inspect batch iterator service to return controlled response to BatchIterator::GetNext().
class StubInspectBatchIteratorBase : public fuchsia::diagnostics::BatchIterator {
 public:
  StubInspectBatchIteratorBase() = default;

  // |fuchsia.diagnostics.BatchIterator|
  virtual void GetNext(GetNextCallback callback) override { FXL_NOTIMPLEMENTED(); }
};

class StubInspectBatchIterator : public StubInspectBatchIteratorBase {
 public:
  StubInspectBatchIterator() : json_batches_({}) {}
  StubInspectBatchIterator(const std::vector<std::vector<std::string>>& json_batches)
      : json_batches_(json_batches) {
    next_json_batch_ = json_batches_.cbegin();
  }

  ~StubInspectBatchIterator();

  // Whether the batch iterator expects at least one more call to GetNext().
  bool ExpectCall() { return next_json_batch_ != json_batches_.cend(); }

  void GetNext(GetNextCallback callback) override;

 private:
  const std::vector<std::vector<std::string>> json_batches_;
  decltype(json_batches_)::const_iterator next_json_batch_;
};

class StubInspectBatchIteratorNeverRespondsAfterOneBatch : public StubInspectBatchIteratorBase {
 public:
  StubInspectBatchIteratorNeverRespondsAfterOneBatch(const std::vector<std::string>& json_batch)
      : json_batch_(json_batch) {}

  void GetNext(GetNextCallback callback) override;

 private:
  const std::vector<std::string> json_batch_;
  bool has_returned_batch_ = false;
};

class StubInspectBatchIteratorNeverResponds : public StubInspectBatchIteratorBase {
 public:
  StubInspectBatchIteratorNeverResponds() {}

  void GetNext(GetNextCallback callback) override;
};

class StubInspectBatchIteratorReturnsError : public StubInspectBatchIteratorBase {
 public:
  StubInspectBatchIteratorReturnsError() {}

  void GetNext(GetNextCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_BATCH_ITERATOR_H_
