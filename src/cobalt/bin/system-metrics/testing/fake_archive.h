// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVE_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

namespace cobalt {

// A faked implementation of ArchiveAccessor that repeatedly returns only one component with the
// given JSON value. Used for testing LogStatsFetcherImpl and ArchivistStatsFetcherImpl.
class FakeArchive : public fuchsia::diagnostics::ArchiveAccessor {
 public:
  explicit FakeArchive(std::string return_value);

  FakeArchive(FakeArchive&&) = default;
  FakeArchive(const FakeArchive&) = delete;
  FakeArchive& operator=(FakeArchive&&) = default;
  FakeArchive& operator=(const FakeArchive&) = delete;

  // ArchiveAccessor implementation:
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters params,
      fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override;

 private:
  class FakeIterator : public fuchsia::diagnostics::BatchIterator {
   public:
    explicit FakeIterator(std::string return_value);

   private:
    // BatchIterator implementation:
    void GetNext(GetNextCallback callback) override;

    bool sent = false;
    std::string return_value_;
  };
  std::unique_ptr<
      fidl::BindingSet<fuchsia::diagnostics::BatchIterator, std::unique_ptr<FakeIterator>>>
      iterator_bindings_;
  std::string return_value_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TESTING_FAKE_ARCHIVE_H_
