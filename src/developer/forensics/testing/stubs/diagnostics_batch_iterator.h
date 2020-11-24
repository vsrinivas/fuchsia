// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_BATCH_ITERATOR_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_BATCH_ITERATOR_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl_test_base.h>

#include <string>
#include <vector>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using DiagnosticsBatchIteratorBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::diagnostics,
                                                                     BatchIterator);

class DiagnosticsBatchIterator : public DiagnosticsBatchIteratorBase {
 public:
  DiagnosticsBatchIterator() : json_batches_({}) {}
  DiagnosticsBatchIterator(const std::vector<std::vector<std::string>>& json_batches)
      : json_batches_(json_batches) {
    next_json_batch_ = json_batches_.cbegin();
  }

  ~DiagnosticsBatchIterator();

  // Whether the batch iterator expects at least one more call to GetNext().
  bool ExpectCall() { return next_json_batch_ != json_batches_.cend(); }

  void GetNext(GetNextCallback callback) override;

 private:
  const std::vector<std::vector<std::string>> json_batches_;
  decltype(json_batches_)::const_iterator next_json_batch_;
};

class DiagnosticsBatchIteratorNeverRespondsAfterOneBatch : public DiagnosticsBatchIteratorBase {
 public:
  DiagnosticsBatchIteratorNeverRespondsAfterOneBatch(const std::vector<std::string>& json_batch)
      : json_batch_(json_batch) {}

  // |fuchsia::diagnostics::BatchIterator|
  void GetNext(GetNextCallback callback) override;

 private:
  const std::vector<std::string> json_batch_;
  bool has_returned_batch_ = false;
};

class DiagnosticsBatchIteratorNeverResponds : public DiagnosticsBatchIteratorBase {
 public:
  DiagnosticsBatchIteratorNeverResponds() {}

  // |fuchsia::diagnostics::BatchIterator|
  STUB_METHOD_DOES_NOT_RETURN(GetNext, GetNextCallback);
};

class DiagnosticsBatchIteratorReturnsError : public DiagnosticsBatchIteratorBase {
 public:
  DiagnosticsBatchIteratorReturnsError() {}

  // |fuchsia::diagnostics::BatchIterator|
  void GetNext(GetNextCallback callback) override;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_BATCH_ITERATOR_H_
