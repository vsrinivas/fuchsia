// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/result.h>
#include <sys/types.h>

#include <sstream>
#include <string>
#include <utility>

#include <fbl/unique_fd.h>

#ifndef SRC_STORAGE_EXTRACTOR_CPP_HEX_DUMP_GENERATOR_H_
#define SRC_STORAGE_EXTRACTOR_CPP_HEX_DUMP_GENERATOR_H_

namespace extractor {

// Options to create HexDumpGenerator.
struct HexDumpGeneratorOptions {
  // Each line of the dump starts with tag, if tag is non-zero length string.
  std::string tag = {};
  // |bytes_per_line| controls the number of bytes of raw data converted into a line.
  size_t bytes_per_line = 64;
  // If |dump_offset| is true the [start, end) of the current line are added to the line.
  bool dump_offset = true;
  // If |dump_checksum| is true then the last line returned by GetNextLine() will contain checksum
  // of the entire file |input|.
  bool dump_checksum = true;
};

// The class provides a streamable interface to hex-dump the contents of a file.
class HexDumpGenerator {
 public:
  // Creates a streamable hexdump of |input| file descriptor.
  static zx::result<std::unique_ptr<HexDumpGenerator>> Create(fbl::unique_fd input,
                                                              HexDumpGeneratorOptions options);

  // Returns the next hexdump line.
  // Note: The function may return 2 lines when the current line happens to be
  // duplicate of previous line.
  // Returns ZX_ERR_STOP if we are done dumping all of the data in the file.
  zx::result<std::string> GetNextLine();

  // Returns true if all the contents of the file have been hex-dumped.
  bool Done() const;

 private:
  HexDumpGenerator(fbl::unique_fd input, off_t file_size, HexDumpGeneratorOptions options)
      : input_(std::move(input)), options_(std::move(options)), file_size_(file_size) {}

  // Returns hexdump of the data in |buffer| which is read from |input_| from offset |offset|.
  zx::result<std::string> DumpToString(const uint8_t* buffer, off_t offset, size_t size);

  // A helper function to generate string considering options like |dump_offset_|, |tag_|, etc.
  std::string BuildLine(const std::string& hex_string, off_t offset, size_t size) const;

  fbl::unique_fd input_;
  HexDumpGeneratorOptions options_;

  // Size of file pointed by |input_|.
  off_t file_size_;

  // Current offset that is being read.
  off_t current_offset_ = 0;

  // This is last returned unique hex-string. It is used to find if the current line is duplicate of
  // previous returned line.
  std::string last_hex_string_;

  // This is used to track start of first duplicate line.
  off_t skip_start_offset_ = 0;

  // Number of bytes present in the duplicate line so far.
  size_t skipped_bytes_ = 0;

  // crc of the file.
  uint32_t crc32_ = 0;
};
}  // namespace extractor

#endif  // SRC_STORAGE_EXTRACTOR_CPP_HEX_DUMP_GENERATOR_H_
