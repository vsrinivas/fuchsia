// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/static_attachments.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <string>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

const std::set<AttachmentKey> kStaticAttachmentKeys = {
    kAttachmentBuildSnapshot,
    kAttachmentLogSystemPrevious,
};

AttachmentValue ReadStringFromFilepath(const std::string& filepath) {
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return AttachmentValue(Error::kFileReadFailure);
  }

  if (content.empty()) {
    return AttachmentValue(Error::kMissingValue);
  }

  return AttachmentValue(content);
}

AttachmentValue ReadAttachmentValueFromFilepath(const AttachmentKey& key,
                                                const std::string& filepath) {
  const auto value = ReadStringFromFilepath(filepath);
  if (!value.HasValue()) {
    FX_LOGS(WARNING) << "Failed to build attachment " << key;
  }
  return value;
}

void CreatePreviousLogsFile(cobalt::Logger* cobalt) {
  // We read the set of /cache files into a single /tmp file.
  system_log_recorder::ProductionDecoder decoder;
  float compression_ratio;
  if (system_log_recorder::Concatenate(kCurrentLogsFilePaths, &decoder, kPreviousLogsFilePath,
                                       &compression_ratio)) {
    FX_LOGS(INFO) << fxl::StringPrintf(
        "Found logs from previous boot cycle (compression ratio %.2f), available at %s\n",
        compression_ratio, kPreviousLogsFilePath);

    cobalt->LogCount(system_log_recorder::ToCobalt(decoder.GetEncodingVersion()),
                     (uint64_t)(compression_ratio * 100));

    // Clean up the /cache files now that they have been concatenated into a single /tmp file.
    for (const auto& file : kCurrentLogsFilePaths) {
      files::DeletePath(file, /*recursive=*/false);
    }
  } else {
    FX_LOGS(WARNING) << "No logs found from previous boot cycle";
  }
}

AttachmentValue BuildAttachmentValue(const AttachmentKey& key, cobalt::Logger* cobalt,
                                     const bool is_first_instance) {
  if (key == kAttachmentBuildSnapshot) {
    return ReadAttachmentValueFromFilepath(key, "/config/build-info/snapshot");
  } else if (key == kAttachmentLogSystemPrevious) {
    // If this is the first instance of the component since boot, we have to create the /tmp log
    // file. Otherwise we can return it immediately if it exists (it wouldn't on a pave for
    // instance).
    //
    if (is_first_instance) {
      FX_CHECK(!std::filesystem::exists(kPreviousLogsFilePath));
      // The /tmp log file is created it by aggregating the content stored in the /cache files for
      // the current boot cycle that are still containing the content from the previous boot cycle.
      //
      // This assumes that the static attachments are fetched before any log persistence for the
      // current boot cycle as this would overwrite these /cache files with the content for the
      // current boot cycle.
      CreatePreviousLogsFile(cobalt);
    }
    return ReadAttachmentValueFromFilepath(key, kPreviousLogsFilePath);
  }
  // There are non-static attachments in the allowlist that we just skip here.
  FX_LOGS(FATAL) << "Invalid attachment key used: " << key;
  return AttachmentValue(Error::kNotSet);
}

AttachmentKeys RestrictAllowlist(const AttachmentKeys& allowlist) {
  AttachmentKeys intersection;
  std::set_intersection(allowlist.begin(), allowlist.end(), kStaticAttachmentKeys.begin(),
                        kStaticAttachmentKeys.end(),
                        std::inserter(intersection, intersection.begin()));

  return intersection;
}

}  // namespace

Attachments GetStaticAttachments(const AttachmentKeys& allowlist, cobalt::Logger* cobalt,
                                 const bool is_first_instance) {
  Attachments attachments;
  for (const auto& key : RestrictAllowlist(allowlist)) {
    attachments.insert({key, BuildAttachmentValue(key, cobalt, is_first_instance)});
  }
  return attachments;
}

}  // namespace feedback_data
}  // namespace forensics
