// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_ARCHIVE_ACCESSOR_STUB_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_ARCHIVE_ACCESSOR_STUB_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

namespace harvester {

class ArchiveAccessorStub : public fuchsia::diagnostics::ArchiveAccessor {
 public:
  ArchiveAccessorStub() = default;

  explicit ArchiveAccessorStub(
      std::unique_ptr<fuchsia::diagnostics::BatchIterator> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request)
      override;

 private:
  std::unique_ptr<fuchsia::diagnostics::BatchIterator> batch_iterator_;
  std::unique_ptr<::fidl::Binding<fuchsia::diagnostics::BatchIterator>>
      batch_iterator_binding_;
};

class BatchIteratorStub : public fuchsia::diagnostics::BatchIterator {
 public:
  BatchIteratorStub() = default;
  explicit BatchIteratorStub(
      const std::vector<std::vector<std::string>>& json_batches)
      : json_batches_(json_batches) {
    next_json_batch_ = json_batches_.cbegin();
  }

  void GetNext(GetNextCallback callback) override;

 private:
  const std::vector<std::vector<std::string>> json_batches_;
  decltype(json_batches_)::const_iterator next_json_batch_;
};

class BatchIteratorReturnsErrorStub
    : public fuchsia::diagnostics::BatchIterator {
 public:
  BatchIteratorReturnsErrorStub() = default;

  void GetNext(GetNextCallback callback) override;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_ARCHIVE_ACCESSOR_STUB_H_
