// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DEBUGDATA_DATASINK_H_
#define LIB_DEBUGDATA_DATASINK_H_

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <cinttypes>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fbl/unique_fd.h>

namespace debugdata {

using DataSinkCallback = fit::function<void(std::string)>;

/// Represents a single dumpfile element.
struct DumpFile {
  std::string name;  // Name of the dumpfile.
  std::string file;  // File name for the content.
};

// Hash implementation provided for using DumpFile in std::unordered_map
struct HashDumpFile {
  size_t operator()(const DumpFile& dump_file) const {
    std::hash<std::string> string_hash;
    size_t hashes[2] = {string_hash(dump_file.name), string_hash(dump_file.file)};
    std::hash<size_t*> size_t_hash;
    return size_t_hash(hashes);
  }
};

inline bool operator==(const DumpFile& left, const DumpFile& right) {
  std::equal_to<> string_eq;
  return string_eq(left.name, right.name) && string_eq(left.file, right.file);
}

// Equivalence implementation provided for using DumpFile in std::unordered_map
struct EqDumpFile {
  bool operator()(const DumpFile& left, const DumpFile& right) const { return left == right; }
};

using DataSinkFileMap =
    std::unordered_map<std::string, std::unordered_set<DumpFile, HashDumpFile, EqDumpFile>>;

// DataSink merges debug data contained in VMOs and dumps the data to the provided directory.
//
// The expected usage is for the caller to pass VMOs with `ProcessSingleDebugData`. After all
// VMOs are processed in this way, the caller should flush the data to directory with
// `FlushToDirectory`.
class DataSink {
 public:
  explicit DataSink(const fbl::unique_fd& data_sink_dir_fd) : data_sink_dir_fd_(data_sink_dir_fd) {}

  // Processes debug data from a single VMO. This function will execute callbacks with error or
  // warnings.
  void ProcessSingleDebugData(const std::string& data_sink, zx::vmo debug_data,
                              DataSinkCallback& error_callback, DataSinkCallback& warning_callback);
  // Flush any data not yet written to disk. Must be called prior to cleaning up `DataSink`.
  // Returns a mapping from data sink name to files written since the last call to
  // `FlushToDirectory`.
  DataSinkFileMap FlushToDirectory(DataSinkCallback& error_callback,
                                   DataSinkCallback& warning_callback);

 private:
  void ProcessProfile(const zx::vmo& data, DataSinkCallback& error_callback,
                      DataSinkCallback& warning_callback);

  const fbl::unique_fd& data_sink_dir_fd_;
  // Buffers grouped by profile name.
  std::unordered_map<std::string, std::pair<std::unique_ptr<uint8_t[]>, uint64_t>> merged_profiles_;
  // Mapping from data sink to dump files.
  DataSinkFileMap dump_files_;
};

/// Processes debug data and returns all files written to `data_sink_dir_fd` and mapped by
/// data_sink. This function will process all data sinks and execute callbacks with error or
/// warnings.
DataSinkFileMap ProcessDebugData(const fbl::unique_fd& data_sink_dir_fd,
                                 std::unordered_map<std::string, std::vector<zx::vmo>> debug_data,
                                 DataSinkCallback error_callback,
                                 DataSinkCallback warning_callback);

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DATASINK_H_
