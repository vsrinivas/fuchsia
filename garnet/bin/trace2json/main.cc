// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <trace-reader/reader.h>

#include "garnet/bin/trace2json/trace_parser.h"
#include "garnet/lib/trace_converters/chromium_exporter.h"

namespace {

const char kInputFile[] = "input-file";
const char kOutputFile[] = "output-file";
const char kNoMagicCheck[] = "no-magic-check";
const char kLittleEndianMagicRecord[8] = {0x10, 0x00, 0x04, 0x46, 0x78, 0x54, 0x16, 0x00};

constexpr size_t kMagicSize = fbl::count_of(kLittleEndianMagicRecord);
constexpr uint64_t kMagicRecord = 0x0016547846040010;

bool CompareMagic(const char* magic1, const char* magic2) {
  for (size_t i = 0; i < kMagicSize; i++) {
    if (magic1[i] != magic2[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  const uint64_t host_magic = kMagicRecord;
  if (!CompareMagic(reinterpret_cast<const char*>(&host_magic), kLittleEndianMagicRecord)) {
    FXL_LOG(ERROR) << "Detected big endian host. Aborting.";
    return 1;
  }

  std::unique_ptr<std::ifstream> input_file_stream;
  std::unique_ptr<std::ofstream> output_file_stream;

  std::istream* in_stream = &std::cin;
  std::ostream* out_stream = &std::cout;

  if (command_line.HasOption(kInputFile)) {
    std::string input_file_name;
    command_line.GetOptionValue(kInputFile, &input_file_name);
    input_file_stream = std::make_unique<std::ifstream>(
        input_file_name.c_str(), std::ios_base::in | std::ios_base::binary);
    in_stream = static_cast<std::istream*>(input_file_stream.get());
  }

  if (!command_line.HasOption(kNoMagicCheck)) {
    // Look for the magic number record at the start of the trace file and bail
    // before opening (and thus truncating) the output file if we don't find it.
    char initial_bytes[kMagicSize];
    if (in_stream->read(initial_bytes, kMagicSize).gcount() != kMagicSize) {
      FXL_LOG(ERROR) << "Failed to read magic number.";
      return 1;
    }
    if (!CompareMagic(initial_bytes, kLittleEndianMagicRecord)) {
      FXL_LOG(ERROR) << "Input file does not start with Fuchsia Trace magic number. Aborting.";
      return 1;
    }
  }

  if (command_line.HasOption(kOutputFile)) {
    std::string output_file_name;
    command_line.GetOptionValue(kOutputFile, &output_file_name);
    output_file_stream = std::make_unique<std::ofstream>(
        output_file_name.c_str(), std::ios_base::out | std::ios_base::trunc);
    out_stream = static_cast<std::ostream*>(output_file_stream.get());
  }

  tracing::FuchsiaTraceParser parser(out_stream);
  if (!parser.ParseComplete(in_stream)) {
    return 1;
  }

  return 0;
}
