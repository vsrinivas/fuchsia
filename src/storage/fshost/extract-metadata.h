// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_EXTRACT_METADATA_H_
#define SRC_STORAGE_FSHOST_EXTRACT_METADATA_H_

#include <lib/zx/time.h>

#include <cstdint>
#include <string>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/format.h"

namespace fshost {

struct DumpMetadataOptions {
  // A string to uniquely identify hex dump strings. This helps in grepping the logs for dump
  // messages. EIL may stand for Extracted Image Log but the string is mostly chosen because of
  // strings rarity in the code base.
  // Try to keep this string as short as possible.
  // |tag| and |bytes_per_line| together decide how long each generated dump line will be. Syslog
  // has its own buffer limit. If a dumped log crosses syslog's limit then syslog might
  // choose to wrap, trucante, or drop the message. We try here to keep tag small and
  // |bytes_per_row| and |stream_buffer_size| large to improve density of the dumped log but still
  // within syslog's limits.
  std::string tag = "EIL";

  // Format of the disk to be extracted.
  fs_management::DiskFormat disk_format = fs_management::kDiskFormatUnknown;

  // How long to wait before and after dumping for logs to settle.
  zx::duration log_settle_time = zx::sec(10);

  // Number of bytes to print per line. See comments for |tag|.
  uint16_t bytes_per_line = 64;

  // Number of bytes to buffer before writing to serial log. See comments for |tag|.
  size_t stream_buffer_size = 10240;
};

// This function returns true if extraction is enabled.
// We link extractor library only in specific build types.
bool ExtractMetadataEnabled();

void MaybeDumpMetadata(fbl::unique_fd device_fd, DumpMetadataOptions options = {});

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_EXTRACT_METADATA_H_
