// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"

#include <assert.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <stdio.h>

#include <cmath>
#include <regex>
#include <sstream>
#include <string>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

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

std::string MakeRepeatedWarning(const size_t repeat_count) {
  if (repeat_count == 1) {
    return kRepeatedOnceFormatStr;
  } else {
    return fxl::StringPrintf(kRepeatedFormatStr, repeat_count);
  }
}

std::string AggregateRepeatedMessages(const std::string_view message) {
  // Combine consecutive repeated log lines into a single warning message.

  // SplitString() adds an empty element at the end.
  std::vector<std::string_view> lines = fxl::SplitString(
      message, "\n", fxl::WhiteSpaceHandling::kKeepWhitespace, fxl::SplitResult::kSplitWantAll);

  std::string output;
  size_t repeat_count = 0;
  for (auto line : lines) {
    if (line.rfind(kRepeatedStrPrefix, 0) == 0) {
      int count;
      // Extract the number of times from the line.
      FX_CHECK(sscanf(line.data(), "!!! MESSAGE REPEATED %d", &count) == 1);
      repeat_count += count;
    } else if (!line.empty()) {
      output += line;
      output += "\n";
    }
  }

  if (repeat_count != 0) {
    output += MakeRepeatedWarning(repeat_count);
  }

  return output;
}

std::string PostProcess(const std::string& log) {
  // Sort the log and aggregate repeated messages by:
  //   1) Splitting it into lines.
  //   2) Merging multiline messages into a single message.
  //   3) Stable sorting the messages by timestamp.
  //   4) Combining consecutive repeated messages together and create the final log.

  // All the operations are on std::string_view because it has shown to be expensive otherwise in
  // practice.

  // Extract the header and the body. The header are the initial log lines that have no timestamps
  // thus do not need sorting, e.g., decoding error messages in the first files - there may be no
  // such lines.
  const size_t header_end = log.find('[');
  const size_t header_size = (header_end == std::string_view::npos) ? log.size() : header_end;

  const std::string_view header(log.data(), header_size);
  const std::string_view body(log.data() + header_size, log.size() - header_size);

  std::vector<std::string_view> lines = fxl::SplitString(
      body, "\n", fxl::WhiteSpaceHandling::kKeepWhitespace, fxl::SplitResult::kSplitWantAll);

  std::vector<std::string_view> messages;

  // Update the end pointer of the last message in |messages|.
  // Note: This also adds the delimeter back if the new_end is the start of the next message.
  auto GrowTailMessage = [&messages](const char* new_end) mutable {
    if (messages.empty()) {
      return;
    }

    messages.back() = std::string_view(messages.back().data(), new_end - messages.back().data());
  };

  auto line = lines.begin();

  // group messages to enable sorting by timestamp.
  while (line != lines.end()) {
    // If a new log message is found, update the last log message to span up until the new message.
    if (MatchesLogMessage(*line)) {
      GrowTailMessage(line->data());
      messages.push_back(*line);
    }
    line++;
  }
  // The last log message needs to span until the end of the log.
  GrowTailMessage(log.data() + log.size());

  std::stable_sort(
      messages.begin(), messages.end(), [](const std::string_view lhs, const std::string_view rhs) {
        const std::string_view lhs_timestamp = lhs.substr(lhs.find('['), lhs.find(']'));
        const std::string_view rhs_timestamp = rhs.substr(rhs.find('['), rhs.find(']'));

        // The timestamp format is "%05d.%03d" so longer strings mean larger timestamps and then we
        // only need to compare the strings if the length is the same.
        if (lhs_timestamp.size() != rhs_timestamp.size()) {
          return lhs_timestamp.size() < rhs_timestamp.size();
        }

        return lhs_timestamp < rhs_timestamp;
      });

  std::string sorted_log;
  sorted_log.reserve(header.size() + log.size());
  sorted_log.append(header);
  for (const auto& message : messages) {
    sorted_log.append(AggregateRepeatedMessages(message));
  }

  return sorted_log;
}

}  // namespace

bool Concatenate(const std::vector<const std::string>& input_file_paths, Decoder* decoder,
                 const std::string& output_file_path, float* compression_ratio) {
  // Set the default compression to NAN in case Concatenate() fails.
  *compression_ratio = NAN;

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
    FX_LOGS(WARNING) << "The encoded previous boot log is empty";
    return false;
  }

  // Decode logs.
  std::string uncompressed_log;
  for (auto path = input_file_paths.crbegin(); path != input_file_paths.crend(); ++path) {
    std::string block;
    if (!files::ReadFileToString(*path, &block)) {
      continue;
    }

    uncompressed_log += decoder->Decode(block);
  }

  if (uncompressed_log.empty()) {
    FX_LOGS(WARNING) << "The decoded previous boot log is empty";
    return false;
  }

  // Sort logs and combine messages for repeated logs.
  uncompressed_log = PostProcess(uncompressed_log);

  if (uncompressed_log.empty()) {
    FX_LOGS(WARNING) << "The post-processed previous boot log is empty";
    return false;
  }

  if (!files::WriteFile(output_file_path, uncompressed_log)) {
    FX_LOGS(WARNING) << "Could not write the previous boot log file: " << output_file_path;
    return false;
  }

  // Compression ratio rounded up to the next decimal, e.g., 2.54x compression -> 2.6x.
  int decimal_ratio = (uncompressed_log.size() * 10 - 1) / total_compressed_log_size + 1;
  *compression_ratio = ((float)decimal_ratio) / 10.0f;

  return true;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
