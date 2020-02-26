// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CONTRIB_CPP_ARCHIVE_READER_H_
#define LIB_INSPECT_CONTRIB_CPP_ARCHIVE_READER_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/scope.h>

#include <rapidjson/document.h>

namespace inspect {
namespace contrib {

// Container for diagnostics data returned by a component.
//
// This class provides methods for parsing common fields from diagnostics output.
class DiagnosticsData {
 public:
  // Create a new DiagnosticsData wrapper around a JSON document.
  explicit DiagnosticsData(rapidjson::Document document);

  // Movable but not copyable.
  DiagnosticsData(const DiagnosticsData&) = delete;
  DiagnosticsData(DiagnosticsData&&) = default;
  DiagnosticsData& operator=(const DiagnosticsData&) = delete;
  DiagnosticsData& operator=(DiagnosticsData&&) = default;

  // Return the name of the component that created this data, if defined.
  const std::string& component_name() const;

  // Return the content of the diagnostics data as a JSON value.
  const rapidjson::Value& content() const;

  // Returns the value at the given path in the data contents.
  const rapidjson::Value& GetByPath(const std::vector<std::string>& path) const;

 private:
  // The document wrapped by this container.
  rapidjson::Document document_;

  // The parsed name of the component.
  std::string name_;
};

// ArchiveReader supports reading Inspect data from an Archive.
class ArchiveReader {
 public:
  // Create a new ArchiveReader.
  //
  // archive: A connected interface pointer to the Archive. Must be bound.
  // selectors: The selectors for data to be returned by this call. Empty means to return all data.
  //
  // Note: This constructor asserts that archive is bound.
  ArchiveReader(fuchsia::diagnostics::ArchivePtr archive, std::vector<std::string> selectors);

  // Get a snapshot of the Inspect data at the current point in time.
  //
  // Returns an error if the ArchivePtr is not bound.
  fit::promise<std::vector<DiagnosticsData>, std::string> GetInspectSnapshot();

  // Gets a snapshot of the Inspect data at the point in time in which all listed component
  // names are present.
  //
  // Returns an error if the ArchivePtr is not bound.
  fit::promise<std::vector<DiagnosticsData>, std::string> SnapshotInspectUntilPresent(
      std::vector<std::string> component_names);

 private:
  void InnerSnapshotInspectUntilPresent(
      fit::completer<std::vector<DiagnosticsData>, std::string> bridge,
      std::vector<std::string> component_names);

  // The pointer to the archive this object is connected to.
  fuchsia::diagnostics::ArchivePtr archive_;

  // The executor on which promise continuations run.
  async::Executor executor_;

  // The selectors used to filter data streamed from this reader.
  std::vector<std::string> selectors_;

  // The scope to tie async task lifetimes to this object.
  fit::scope scope_;
};

}  // namespace contrib
}  // namespace inspect

#endif  // LIB_INSPECT_CONTRIB_CPP_ARCHIVE_READER_H_
