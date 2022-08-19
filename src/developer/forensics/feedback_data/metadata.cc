// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/metadata.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/utc.h>

#include <optional>
#include <set>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/errors.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/split_string.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics {
namespace feedback_data {
namespace {

using namespace rapidjson;

std::set<std::string> kUtcMonotonicDifferenceAllowlist = {
    kAttachmentInspect,
    kAttachmentLogKernel,
    kAttachmentLogSystem,
};

std::set<std::string> kPreviousBootUtcMonotonicDifferenceAllowlist = {
    kAttachmentLogSystemPrevious,
};

std::string ToString(const enum feedback::AttachmentValue::State state) {
  switch (state) {
    case feedback::AttachmentValue::State::kComplete:
      return "complete";
    case feedback::AttachmentValue::State::kPartial:
      return "partial";
    case feedback::AttachmentValue::State::kMissing:
      return "missing";
  }
}

// Create a complete list set of annotations from the collected annotations and the allowlist.
feedback::Annotations AllAnnotations(const std::set<std::string>& allowlist,
                                     const feedback::Annotations& annotations) {
  feedback::Annotations all_annotations = annotations;

  for (const auto& key : allowlist) {
    if (all_annotations.find(key) == all_annotations.end()) {
      // There is an annotation in the allowlist that was not produced by any provider. This
      // indicates a logical error on the Feedback-side.
      all_annotations.insert({key, Error::kLogicError});
    }
  }

  return all_annotations;
}

// Create a complete list set of attachments from the collected attachments and the allowlist.
feedback::Attachments AllAttachments(const feedback::AttachmentKeys& allowlist,
                                     const feedback::Attachments& attachments) {
  feedback::Attachments all_attachments;

  // Because attachments can contain large blobs of text and we only care about the state of the
  // attachment and its associated error, we don't copy the value of the attachment.
  for (const auto& [k, v] : attachments) {
    switch (v.State()) {
      case feedback::AttachmentValue::State::kComplete:
        all_attachments.insert({k, feedback::AttachmentValue("")});
        break;
      case feedback::AttachmentValue::State::kPartial:
        all_attachments.insert({k, feedback::AttachmentValue("", v.Error())});
        break;
      case feedback::AttachmentValue::State::kMissing:
        all_attachments.insert({k, v});
        break;
    }
  }

  for (const auto& key : allowlist) {
    if (all_attachments.find(key) == all_attachments.end()) {
      all_attachments.insert({key, feedback::AttachmentValue(Error::kLogicError)});
    }
  }

  return all_attachments;
}

void AddUtcMonotonicDifference(const std::optional<zx::duration>& utc_monotonic_difference,
                               Value* file, Document::AllocatorType& allocator) {
  if (!utc_monotonic_difference.has_value() || !file->IsObject() ||
      file->HasMember("utc_monotonic_difference_nanos") ||
      (file->HasMember("state") && (*file)["state"].IsString() &&
       (*file)["state"].GetString() == ToString(feedback::AttachmentValue::State::kMissing))) {
    return;
  }

  file->AddMember("utc_monotonic_difference_nanos",
                  Value().SetInt64(utc_monotonic_difference.value().get()), allocator);
}

void AddUtcMonotonicDifferences(
    const std::optional<zx::duration> utc_monotonic_difference,
    const std::optional<zx::duration> previous_boot_utc_monotonic_difference,
    Document* metadata_json) {
  if (!metadata_json->HasMember("files")) {
    return;
  }

  for (auto& file : (*metadata_json)["files"].GetObject()) {
    if (kUtcMonotonicDifferenceAllowlist.find(file.name.GetString()) !=
        kUtcMonotonicDifferenceAllowlist.end()) {
      AddUtcMonotonicDifference(utc_monotonic_difference, &file.value,
                                metadata_json->GetAllocator());
    }

    if (kPreviousBootUtcMonotonicDifferenceAllowlist.find(file.name.GetString()) !=
        kPreviousBootUtcMonotonicDifferenceAllowlist.end()) {
      AddUtcMonotonicDifference(previous_boot_utc_monotonic_difference, &file.value,
                                metadata_json->GetAllocator());
    }
  }
}

void AddAttachments(const feedback::AttachmentKeys& attachment_allowlist,
                    const feedback::Attachments& attachments, Document* metadata_json) {
  if (attachment_allowlist.empty()) {
    return;
  }

  auto& allocator = metadata_json->GetAllocator();
  auto MakeValue = [&allocator](const std::string& v) { return Value(v, allocator); };

  for (const auto& [name, v] : AllAttachments(attachment_allowlist, attachments)) {
    Value file(kObjectType);

    file.AddMember("state", MakeValue(ToString(v.State())), allocator);
    if (v.HasError()) {
      file.AddMember("error", MakeValue(ToReason(v.Error())), allocator);
    }

    (*metadata_json)["files"].AddMember(MakeValue(name), file, allocator);
  }
}

void AddAnnotationsJson(const std::set<std::string>& annotation_allowlist,
                        const feedback::Annotations& annotations,
                        const bool missing_non_platform_annotations, Document* metadata_json) {
  const feedback::Annotations all_annotations = AllAnnotations(annotation_allowlist, annotations);

  bool has_non_platform = all_annotations.size() > annotation_allowlist.size();
  if (annotation_allowlist.empty() && !(has_non_platform || missing_non_platform_annotations)) {
    return;
  }

  auto& allocator = metadata_json->GetAllocator();
  auto MakeValue = [&allocator](const std::string& v) { return Value(v, allocator); };

  Value present(kArrayType);
  Value missing(kObjectType);

  size_t num_present_platform = 0u;
  size_t num_missing_platform = 0u;
  for (const auto& [k, v] : all_annotations) {
    if (annotation_allowlist.find(k) == annotation_allowlist.end()) {
      continue;
    }

    Value key(MakeValue(k));
    if (v.HasValue()) {
      present.PushBack(key, allocator);
      ++num_present_platform;
    } else {
      missing.AddMember(key, MakeValue(ToReason(v.Error())), allocator);
      ++num_missing_platform;
    }
  }

  if (has_non_platform || missing_non_platform_annotations) {
    if (!missing_non_platform_annotations) {
      present.PushBack("non-platform annotations", allocator);
    } else {
      missing.AddMember("non-platform annotations", "too many non-platfrom annotations added",
                        allocator);
    }
  }

  Value state;
  if (num_present_platform == annotation_allowlist.size() && !missing_non_platform_annotations) {
    state = "complete";
  } else if (num_missing_platform == annotation_allowlist.size() && !has_non_platform &&
             missing_non_platform_annotations) {
    state = "missing";
  } else {
    state = "partial";
  }

  Value annotations_json(kObjectType);
  annotations_json.AddMember("state", state, allocator);
  annotations_json.AddMember("missing annotations", missing, allocator);
  annotations_json.AddMember("present annotations", present, allocator);

  (*metadata_json)["files"].AddMember("annotations.json", annotations_json, allocator);
}

void AddLogRedactionCanary(const std::string& log_redaction_canary, Document* metadata_json) {
  auto& allocator = metadata_json->GetAllocator();
  Value lines(kArrayType);
  for (const std::string& line :
       fxl::SplitStringCopy(log_redaction_canary, "\n", fxl::WhiteSpaceHandling::kTrimWhitespace,
                            fxl::SplitResult::kSplitWantNonEmpty)) {
    lines.PushBack(Value(line, allocator), allocator);
  }

  metadata_json->AddMember("log_redaction_canary", lines, allocator);
}

}  // namespace

Metadata::Metadata(async_dispatcher_t* dispatcher, timekeeper::Clock* clock, RedactorBase* redactor,
                   const bool is_first_instance, const std::set<std::string>& annotation_allowlist,
                   const feedback::AttachmentKeys& attachment_allowlist)
    : log_redaction_canary_(redactor->UnredactedCanary()),
      annotation_allowlist_(annotation_allowlist),
      attachment_allowlist_(attachment_allowlist),
      utc_clock_ready_watcher_(dispatcher, zx::unowned_clock(zx_utc_reference_get())),
      utc_provider_(&utc_clock_ready_watcher_, clock,
                    PreviousBootFile::FromCache(is_first_instance, kUtcMonotonicDifferenceFile)) {
  redactor->Redact(log_redaction_canary_);
}

std::string Metadata::MakeMetadata(const feedback::Annotations& annotations,
                                   const feedback::Attachments& attachments,
                                   const std::string& snapshot_uuid,
                                   bool missing_non_platform_annotations) {
  Document metadata_json(kObjectType);
  auto& allocator = metadata_json.GetAllocator();

  auto MetadataString = [&metadata_json]() {
    StringBuffer buffer;
    PrettyWriter<StringBuffer> writer(buffer);

    metadata_json.Accept(writer);

    return std::string(buffer.GetString());
  };

  // Insert all top-level fields
  metadata_json.AddMember("snapshot_version", Value(SnapshotVersion::kString, allocator),
                          allocator);
  metadata_json.AddMember("metadata_version", Value(Metadata::kVersion, allocator), allocator);
  metadata_json.AddMember("snapshot_uuid", Value(snapshot_uuid, allocator), allocator);
  metadata_json.AddMember("files", Value(kObjectType), allocator);
  AddLogRedactionCanary(log_redaction_canary_, &metadata_json);

  const bool has_non_platform_annotations = annotations.size() > annotation_allowlist_.size();

  if (annotation_allowlist_.empty() && attachment_allowlist_.empty() &&
      !has_non_platform_annotations && !missing_non_platform_annotations) {
    return MetadataString();
  }

  AddAttachments(attachment_allowlist_, attachments, &metadata_json);
  AddAnnotationsJson(annotation_allowlist_, annotations, missing_non_platform_annotations,
                     &metadata_json);
  AddUtcMonotonicDifferences(utc_provider_.CurrentUtcMonotonicDifference(),
                             utc_provider_.PreviousBootUtcMonotonicDifference(), &metadata_json);

  return MetadataString();
}

}  // namespace feedback_data
}  // namespace forensics
