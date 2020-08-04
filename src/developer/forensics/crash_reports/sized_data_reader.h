// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SIZED_DATA_READER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SIZED_DATA_READER_H_

#include "src/developer/forensics/utils/sized_data.h"
#include "third_party/crashpad/util/file/file_reader.h"

namespace forensics {
namespace crash_reports {

// Wrapper around SizedData that allows crashpad::HTTPMultipartBuilder to upload
// attachments. This class operates similarly to crashpad::StringFile, but lacks the interface
// write to the underlying object.
class SizedDataReader : public crashpad::FileReaderInterface {
 public:
  SizedDataReader(const SizedData& data) : data_(data), offset_(0u) {}

  // crashpad::FileReaderInterface
  crashpad::FileOperationResult Read(void* data, size_t size) override;

  // crashpad::FileSeekerInterface
  crashpad::FileOffset Seek(crashpad::FileOffset offset, int whence) override;

 private:
  const SizedData& data_;
  size_t offset_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SIZED_DATA_READER_H_
