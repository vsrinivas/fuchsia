// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extract-metadata.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/extractor/cpp/hex_dump_generator.h"
#include "src/storage/minfs/format.h"

namespace fshost {
namespace {

void Dump(fbl::unique_fd image_fd, DumpMetadataOptions& dump_options) {
  lseek(image_fd.get(), 0, SEEK_SET);
  extractor::HexDumpGeneratorOptions options = {
      .tag = dump_options.tag,
      .bytes_per_line = dump_options.bytes_per_line,
      .dump_offset = true,
      .dump_checksum = true,
  };
  auto generator_or = extractor::HexDumpGenerator::Create(std::move(image_fd), options);

  if (generator_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to create hex dump generator: "
                   << zx_status_get_string(generator_or.error_value());
    return;
  }

  auto generator = std::move(generator_or.value());

  // Force start dump on a new line. FX_LOGS(ERROR) prints component name, file path and line number
  // along with the message. This might wrap the log around and make it difficult to grep. So we add
  // a new line here.
  std::string buffer = "\n";
  while (!generator->Done()) {
    auto line_or = generator->GetNextLine();
    if (line_or.is_error()) {
      // Dump whatever we read so far and return.
      FX_LOGS(ERROR) << buffer;
      FX_LOGS(ERROR) << "Failed to get hex dump line"
                     << zx_status_get_string(line_or.error_value());
      return;
    }
    buffer.append(line_or.value());
    if (buffer.length() > dump_options.stream_buffer_size) {
      FX_LOGS(ERROR) << buffer;
      // Force start dump on a new line. FX_LOGS(ERROR) prints component name, file path and line
      // number along with the message. This might wrap the log around and make it difficult to
      // grep. So we add a new line here.
      buffer = "\n";
    }
  }

  if (buffer.length() > 1) {
    FX_LOGS(ERROR) << buffer;
  }
}

}  // namespace

bool ExtractMetadataEnabled() { return true; }

void MaybeDumpMetadata(fbl::unique_fd device_fd, DumpMetadataOptions options) {
  // At the moment, extraction is supported only for minfs.
  ZX_ASSERT(options.disk_format == fs_management::kDiskFormatMinfs);
  if (options.bytes_per_line <= 0) {
    FX_LOGS(ERROR) << "Invalid bytes_per_line:" << options.bytes_per_line << std::endl;
    return;
  }

  if (options.stream_buffer_size <= 0) {
    FX_LOGS(ERROR) << "Invalid stream_buffer_size:" << options.stream_buffer_size << std::endl;
    return;
  }

  if (!device_fd) {
    FX_LOGS(ERROR) << "Invalid device for extractor" << std::endl;
    return;
  }

  std::string tmp_path = "/fs/tmp/extracted_image_XXXXXX";
  fbl::unique_fd output_stream(mkstemp(tmp_path.data()));
  if (!output_stream) {
    FX_LOGS(ERROR) << "Failed to create image file: " << tmp_path;
    return;
  }

  ExtractorOptions extractor_options{
      .force_dump_pii = false,
      .add_checksum = false,
      .alignment = minfs::kMinfsBlockSize,
      // TODO(fxbug.dev/67782): Enable compression.
      .compress = false,
  };

  auto extractor_or = extractor::Extractor::Create(device_fd.duplicate(), extractor_options,
                                                   output_stream.duplicate());

  if (extractor_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to create extractor: "
                   << zx_status_get_string(extractor_or.error_value());
    return;
  }

  if (auto status = extractor::MinfsExtract(std::move(device_fd), *extractor_or.value());
      status.is_error()) {
    FX_LOGS(ERROR) << "Failed to extract: " << zx_status_get_string(status.error_value());
    return;
  }
  if (auto status = extractor_or.value()->Write(); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to write to the extracted file: "
                   << zx_status_get_string(status.error_value());
    return;
  }

  // Wait for all other component to stop writing to logs. This is not fool proof but helps to
  // cluster logs together and decreases the chances of dropping logs.
  zx::nanosleep(zx::deadline_after(options.log_settle_time));

  FX_LOGS(ERROR) << std::endl
                 << options.tag << ": Extracting minfs to serial." << std::endl
                 << options.tag << ": Following lines that start with \"" << options.tag
                 << "\" are from extractor." << std::endl
                 << options.tag << ": Successful extraction ends with \"" << options.tag
                 << ": Done extracting minfs to serial.\"" << std::endl
                 << options.tag << ": Compression:" << (extractor_options.compress ? "on" : "off")
                 << " Checksum:on Offset:on bytes_per_line:" << options.bytes_per_line << std::endl;

  Dump(std::move(output_stream), options);

  FX_LOGS(ERROR) << std::endl << options.tag << ": Done extracting minfs to serial" << std::endl;
  // Wait for all the logs we have written to get flushed. This is not fool proof but helps to
  // cluster logs together and decreases the chances of hex-dump logs interleaving with other logs.
  zx::nanosleep(zx::deadline_after(options.log_settle_time));
}

}  // namespace fshost
