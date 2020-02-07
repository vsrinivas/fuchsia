// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_READER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_READER_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/tests/stub_inspect_batch_iterator.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

// Stub Inspect reader service to return controlled response to Reader::GetSnapshot().
class StubInspectReader : public fuchsia::diagnostics::Reader {
 public:
  StubInspectReader() {}
  StubInspectReader(std::unique_ptr<StubInspectBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  // |fuchsia.diagnostics.Reader|
  void GetSnapshot(fuchsia::diagnostics::Format format,
                   fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                   GetSnapshotCallback callback) override;
  void ReadStream(fuchsia::diagnostics::StreamMode stream_mode, fuchsia::diagnostics::Format format,
                  fidl::InterfaceRequest<fuchsia::diagnostics::Stream> request) override {
    FXL_NOTIMPLEMENTED();
  }

 protected:
  std::unique_ptr<StubInspectBatchIteratorBase> batch_iterator_;
  std::unique_ptr<fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class StubInspectReaderClosesBatchIteratorConnection : public StubInspectReader {
 public:
  StubInspectReaderClosesBatchIteratorConnection(
      std::unique_ptr<StubInspectBatchIterator> batch_iterator)
      : StubInspectReader(std::move(batch_iterator)) {}

  void GetSnapshot(fuchsia::diagnostics::Format format,
                   fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                   GetSnapshotCallback callback) override;
};

class StubInspectReaderNeverResponds : public StubInspectReader {
 public:
  StubInspectReaderNeverResponds() {}

  void GetSnapshot(fuchsia::diagnostics::Format format,
                   fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                   GetSnapshotCallback callback) override;
};

class StubInspectReaderReturnsError : public StubInspectReader {
 public:
  StubInspectReaderReturnsError() {}

  void GetSnapshot(fuchsia::diagnostics::Format format,
                   fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                   GetSnapshotCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_READER_H_
