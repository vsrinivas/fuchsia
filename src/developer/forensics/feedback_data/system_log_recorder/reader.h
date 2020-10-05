// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_READER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_READER_H_

#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/decoder.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// TODO(fxbug.dev/61101): Remove |input_file_paths| once |logs_dir| is used everywhere.
bool Concatenate(const std::vector<const std::string>& input_file_paths,
                 const std::string& logs_dir, Decoder* decoder, const std::string& output_file_path,
                 float* compression_ratio);

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_READER_H_
