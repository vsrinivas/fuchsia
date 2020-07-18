// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"

#include <assert.h>
#include <lib/trace/event.h>

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

// Check if the start of |line| is formatted like a log message by checking that the timestamp, pid,
// and tid tags are present. The formatting is defined in
// //src/developer/forensics/utils/log_format.h.
//
// Note: this definition of this function needs to in the same file as SortLog otherwise. We
// experienced significant performance issue when this was not done and the log being sorted was
// large.
bool MatchesLogMessage(std::string_view line) {
  std::regex line_start("^\\[\\d{5,9}\\.\\d{3}\\]\\[\\d{5,9}\\]\\[\\d{5,9}\\]");
  return std::regex_search(line.cbegin(), line.cend(), line_start);
}

std::string SortLog(const std::string& log) {
  // Sort the log by:
  //   1) Splitting it into lines.
  //   2) Merging multiline messages into a single message.
  //   3) Stable sorting the messages by timestamp.
  //   4) Combining the messages into a sorted log.

  std::vector<std::string_view> lines = fxl::SplitString(
      log, "\n", fxl::WhiteSpaceHandling::kKeepWhitespace, fxl::SplitResult::kSplitWantAll);

  std::vector<std::string_view> messages;

  // Update the end pointer of the last message in |messages|.
  auto GrowTailMessage = [&messages](const char* new_end) mutable {
    if (messages.empty()) {
      return;
    }

    messages.back() = std::string_view(messages.back().data(), new_end - messages.back().data());
  };

  for (const std::string_view& line : lines) {
    // If a new log message is found, update the last log message to span up until the new message.
    if (MatchesLogMessage(line)) {
      GrowTailMessage(line.data());
      messages.push_back(line);
    }
  }
  // The last log message needs to span until the end of the log.
  GrowTailMessage(log.data() + log.size());

  std::stable_sort(
      messages.begin(), messages.end(), [](const std::string_view lhs, const std::string_view rhs) {
        const std::string_view lhs_timestamp = lhs.substr(lhs.find('['), lhs.find(']'));
        const std::string_view rhs_timestamp = rhs.substr(rhs.find('['), rhs.find(']'));
        return lhs_timestamp < rhs_timestamp;
      });

  std::string sorted_log;
  sorted_log.reserve(log.size());
  for (const auto& message : messages) {
    sorted_log.append(message);
  }

  return sorted_log;
}

}  // namespace

bool Concatenate(const std::vector<const std::string>& input_file_paths, Decoder* decoder,
                 const std::string& output_file_path, float* compression_ratio) {
  uint64_t total_compressed_log_size{0};
  for (auto path = input_file_paths.crbegin(); path != input_file_paths.crend(); ++path) {
    uint64_t size;
    // To get a valid size, the file must exist!
    if (files::IsFile(*path)) {
      files::GetFileSize(*path, &size);
      total_compressed_log_size += size;
    }
  }

  if (total_compressed_log_size == 0) {
    return false;
  }

  std::string uncompressed_log;
  for (auto path = input_file_paths.crbegin(); path != input_file_paths.crend(); ++path) {
    std::string block;
    if (!files::ReadFileToString(*path, &block)) {
      continue;
    }

    uncompressed_log += decoder->Decode(block);
  }

  if (uncompressed_log.empty()) {
    *compression_ratio = NAN;
    return false;
  }

  *compression_ratio = ((float)uncompressed_log.size()) / (float)total_compressed_log_size;

  return files::WriteFile(output_file_path, SortLog(uncompressed_log));
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
