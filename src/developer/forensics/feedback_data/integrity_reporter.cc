// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/integrity_reporter.h"

#include "src/developer/forensics/feedback_data/errors.h"
#include "src/developer/forensics/utils/errors.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "zircon/third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics {
namespace feedback_data {
namespace {

std::string ToString(const enum AttachmentValue::State state) {
  switch (state) {
    case AttachmentValue::State::kComplete:
      return "complete";
    case AttachmentValue::State::kPartial:
      return "partial";
    case AttachmentValue::State::kMissing:
      return "missing";
  }
}

// Create a complete list set of annotations from the collected annotations and the allowlist.
Annotations AllAnnotations(const AnnotationKeys& allowlist,
                           const ::fit::result<Annotations>& annotations_result) {
  Annotations all_annotations;
  if (annotations_result.is_ok()) {
    all_annotations.insert(annotations_result.value().cbegin(), annotations_result.value().cend());
  }

  for (const auto& key : allowlist) {
    if (all_annotations.find(key) == all_annotations.end()) {
      // There is an annotation in the allowlist that was not produced by any provider. This
      // indicates a logical error on the Feedback-side.
      all_annotations.insert({key, AnnotationOr(Error::kLogicError)});
    }
  }

  return all_annotations;
}

// Create a complete list set of attachments from the collected attachments and the allowlist.
Attachments AllAttachments(const AttachmentKeys& allowlist,
                           const ::fit::result<Attachments>& attachments_result) {
  Attachments all_attachments;
  if (attachments_result.is_ok()) {
    // Because attachments can contain large blobs of text and we only care about the state of the
    // attachment and its associated error, we don't copy the value of the attachment.
    for (const auto& [k, v] : attachments_result.value()) {
      switch (v.State()) {
        case AttachmentValue::State::kComplete:
          all_attachments.insert({k, AttachmentValue("")});
          break;
        case AttachmentValue::State::kPartial:
          all_attachments.insert({k, AttachmentValue("", v.Error())});
          break;
        case AttachmentValue::State::kMissing:
          all_attachments.insert({k, v});
          break;
      }
    }
  }

  for (const auto& key : allowlist) {
    if (all_attachments.find(key) == all_attachments.end()) {
      all_attachments.insert({key, AttachmentValue(Error::kLogicError)});
    }
  }

  return all_attachments;
}

void AddAttachments(const AttachmentKeys& attachment_allowlist,
                    const ::fit::result<Attachments>& attachments_result,
                    rapidjson::Document* integrity_report) {
  if (attachment_allowlist.empty()) {
    return;
  }

  rapidjson::Document::AllocatorType& allocator = integrity_report->GetAllocator();

  for (const auto& [name, v] : AllAttachments(attachment_allowlist, attachments_result)) {
    rapidjson::Value attachment(rapidjson::kObjectType);
    attachment.AddMember("state", rapidjson::Value(ToString(v.State()), allocator), allocator);
    if (v.HasError()) {
      attachment.AddMember("reason", rapidjson::Value(ToReason(v.Error()), allocator), allocator);
    }

    integrity_report->AddMember(rapidjson::Value(name, allocator), attachment, allocator);
  }
}

void AddAnnotationsJson(const AnnotationKeys& annotation_allowlist,
                        const ::fit::result<Annotations>& annotations_result,
                        const bool missing_non_platform_annotations,
                        rapidjson::Document* integrity_report) {
  const Annotations all_annotations = AllAnnotations(annotation_allowlist, annotations_result);

  bool has_non_platform = all_annotations.size() > annotation_allowlist.size();
  if (annotation_allowlist.empty() && !(has_non_platform || missing_non_platform_annotations)) {
    return;
  }

  rapidjson::Document::AllocatorType& allocator = integrity_report->GetAllocator();

  rapidjson::Value present(rapidjson::kArrayType);
  rapidjson::Value missing(rapidjson::kObjectType);

  size_t num_present_platform = 0u;
  size_t num_missing_platform = 0u;
  for (const auto& [k, v] : all_annotations) {
    if (annotation_allowlist.find(k) == annotation_allowlist.end()) {
      continue;
    }

    rapidjson::Value key(k, allocator);
    if (v.HasValue()) {
      present.PushBack(key, allocator);
      ++num_present_platform;
    } else {
      missing.AddMember(key, rapidjson::Value(ToReason(v.Error()), allocator), allocator);
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

  rapidjson::Value state;
  if (num_present_platform == annotation_allowlist.size() && !missing_non_platform_annotations) {
    state = "complete";
  } else if (num_missing_platform == annotation_allowlist.size() && !has_non_platform &&
             missing_non_platform_annotations) {
    state = "missing";
  } else {
    state = "partial";
  }

  rapidjson::Value annotations_json(rapidjson::kObjectType);
  annotations_json.AddMember("state", state, allocator);
  annotations_json.AddMember("missing annotations", missing, allocator);
  annotations_json.AddMember("present annotations", present, allocator);

  integrity_report->AddMember("annotations.json", annotations_json, allocator);
}

}  // namespace

IntegrityReporter::IntegrityReporter(const AnnotationKeys& annotation_allowlist,
                                     const AttachmentKeys& attachment_allowlist)
    : annotation_allowlist_(annotation_allowlist), attachment_allowlist_(attachment_allowlist) {}

std::optional<std::string> IntegrityReporter::MakeIntegrityReport(
    const ::fit::result<Annotations>& annotations_result,
    const ::fit::result<Attachments>& attachments_result,
    bool missing_non_platform_annotations) const {
  const bool has_non_platform_annotations =
      annotations_result.is_ok() &&
      annotations_result.value().size() > annotation_allowlist_.size();

  if (annotation_allowlist_.empty() && attachment_allowlist_.empty() &&
      !has_non_platform_annotations && !missing_non_platform_annotations) {
    return std::nullopt;
  }

  rapidjson::Document integrity_report(rapidjson::kObjectType);

  AddAttachments(attachment_allowlist_, attachments_result, &integrity_report);
  AddAnnotationsJson(annotation_allowlist_, annotations_result, missing_non_platform_annotations,
                     &integrity_report);

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

  integrity_report.Accept(writer);

  return buffer.GetString();
}

}  // namespace feedback_data
}  // namespace forensics
