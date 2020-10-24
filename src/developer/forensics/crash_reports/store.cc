// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/store.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <set>
#include <vector>

#include "src/developer/forensics/crash_reports/snapshot_manager.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "zircon/third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics {
namespace crash_reports {
namespace {

constexpr char kAnnotationsFilename[] = "annotations.json";
constexpr char kMinidumpFilename[] = "minidump.dmp";
constexpr char kSnapshotUuidFilename[] = "snapshot_uuid.txt";

const std::set<std::string> kReservedAttachmentNames = {
    kAnnotationsFilename,
    kMinidumpFilename,
    kSnapshotUuidFilename,
};

// Recursively delete |path|.
bool DeletePath(const std::string& path) { return files::DeletePath(path, /*recursive=*/true); }

// Get the contents of a directory without ".".
std::vector<std::string> GetDirectoryContents(const std::string& dir) {
  std::vector<std::string> contents;
  files::ReadDirContents(dir, &contents);

  contents.erase(std::remove(contents.begin(), contents.end(), "."), contents.end());
  return contents;
}

// Recursively delete empty directories under |root|, including |root| if it is empty or becomes
// empty.
void RemoveEmptyDirectories(const std::string& root) {
  std::vector<std::string> contents = GetDirectoryContents(root);
  if (contents.empty()) {
    DeletePath(root);
    return;
  }

  for (const auto& content : contents) {
    const std::string path = files::JoinPath(root, content);
    if (files::IsDirectory(path)) {
      RemoveEmptyDirectories(path);
    }
  }

  if (GetDirectoryContents(root).empty()) {
    DeletePath(root);
  }
}

std::string FormatAnnotationsAsJson(const std::map<std::string, std::string>& annotations) {
  rapidjson::Document json(rapidjson::kObjectType);
  auto& allocator = json.GetAllocator();

  for (const auto& [k, v] : annotations) {
    auto key = rapidjson::Value(k, allocator);
    auto val = rapidjson::Value(v, allocator);
    json.AddMember(key, val, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

  json.Accept(writer);

  return buffer.GetString();
}

bool ReadAnnotations(const std::string& path, std::map<std::string, std::string>* annotations) {
  std::string json;
  if (!files::ReadFileToString(path, &json)) {
    return false;
  }

  rapidjson::Document document;
  document.Parse(json);

  if (!document.IsObject()) {
    return false;
  }

  std::map<std::string, std::string> local_annotations;
  for (const auto& member : document.GetObject()) {
    if (!member.value.IsString()) {
      return false;
    }

    local_annotations[member.name.GetString()] = member.value.GetString();
  }

  local_annotations.swap(*annotations);
  return true;
}

template <typename T>
bool WriteData(const std::string& path, const T& attachment) {
  return files::WriteFile(path, reinterpret_cast<const char*>(attachment.data()),
                          attachment.size());
}

bool ReadAttachment(const std::string& path, SizedData* attachment) {
  return files::ReadFileToVector(path, attachment);
}

bool ReadSnapshotUuid(const std::string& path, SnapshotUuid* snapshot_uuid) {
  return files::ReadFileToString(path, snapshot_uuid);
}

}  // namespace

Store::Store(std::shared_ptr<InfoContext> info, const std::string& root_dir, StorageSize max_size)
    : root_dir_(root_dir), metadata_(root_dir, max_size), info_(std::move(info)) {
  info_.LogMaxStoreSize(max_size);

  // Clean up any empty directories under |root_dir_|. This may happen if the component stops
  // running while it is deleting a report.
  RemoveEmptyDirectories(root_dir_);

  metadata_.RecreateFromFilesystem();
}

bool Store::Add(const ReportId report_id, Report report,
                std::vector<ReportId>* garbage_collected_reports) {
  if (metadata_.Contains(report_id)) {
    FX_LOGS(ERROR) << "Store already contains report for crash " << report_id;
    return false;
  }

  for (const auto& key : kReservedAttachmentNames) {
    if (report.Attachments().find(key) != report.Attachments().end()) {
      FX_LOGS(ERROR) << "Attachment is using reserved key: " << key;
      return false;
    }
  }

  const std::string program_dir = files::JoinPath(root_dir_, report.ProgramShortname());
  const std::string report_dir = files::JoinPath(program_dir, std::to_string(report_id));

  auto cleanup_on_error = fit::defer([report_dir] { DeletePath(report_dir); });

  if (!files::CreateDirectory(report_dir)) {
    FX_LOGS(ERROR) << "Failed to create directory for report: " << report_dir;
    return false;
  }

  const std::string annotations_json = FormatAnnotationsAsJson(report.Annotations());

  // Organize the report attachments.
  auto attachments = std::move(report.Attachments());
  attachments.emplace(kAnnotationsFilename,
                      SizedData(annotations_json.begin(), annotations_json.end()));
  attachments.emplace(kSnapshotUuidFilename,
                      SizedData(report.SnapshotUuid().begin(), report.SnapshotUuid().end()));
  if (report.Minidump().has_value()) {
    attachments.emplace(kMinidumpFilename, std::move(report.Minidump().value()));
  }

  // Determine the size of the report.
  StorageSize report_size = StorageSize::Bytes(0u);
  for (const auto& [_, data] : attachments) {
    report_size += StorageSize::Bytes(data.size());
  }

  // Ensure there's enough space in the store for the report.
  if (!MakeFreeSpace(report_size, garbage_collected_reports)) {
    FX_LOGS(ERROR) << "Failed to make space for report";
    return false;
  }

  std::vector<std::string> attachment_keys;
  for (const auto& [key, data] : attachments) {
    attachment_keys.push_back(key);

    // Write the report's content to the the filesystem.
    if (!WriteData(files::JoinPath(report_dir, key), data)) {
      FX_LOGS(ERROR) << "Failed to write attachment " << key;
      return false;
    }
  }

  metadata_.Add(report_id, report.ProgramShortname(), std::move(attachment_keys), report_size);

  cleanup_on_error.cancel();
  return true;
}

std::optional<Report> Store::Get(const ReportId report_id) {
  if (!metadata_.Contains(report_id)) {
    return std::nullopt;
  }

  auto attachment_files = metadata_.ReportAttachments(report_id, /*absolute_paths=*/false);
  auto attachment_paths = metadata_.ReportAttachments(report_id, /*absolute_paths=*/true);

  if (attachment_files.empty()) {
    return std::nullopt;
  }

  std::map<std::string, std::string> annotations;
  std::map<std::string, SizedData> attachments;
  SnapshotUuid snapshot_uuid;
  std::optional<SizedData> minidump;

  for (size_t i = 0; i < attachment_files.size(); ++i) {
    if (attachment_files[i] == "annotations.json") {
      if (!ReadAnnotations(attachment_paths[i], &annotations)) {
        return std::nullopt;
      }
    } else if (attachment_files[i] == "snapshot_uuid.txt") {
      if (!ReadSnapshotUuid(attachment_paths[i], &snapshot_uuid)) {
        snapshot_uuid = SnapshotManager::UuidForNoSnapshotUuid();
      }

    } else {
      SizedData attachment;
      if (!ReadAttachment(attachment_paths[i], &attachment)) {
        return std::nullopt;
      }

      if (attachment_files[i] == "minidump.dmp") {
        minidump = std::move(attachment);
      } else {
        attachments.emplace(attachment_files[i], std::move(attachment));
      }
    }
  }

  return Report(metadata_.ReportProgram(report_id), std::move(annotations), std::move(attachments),
                std::move(snapshot_uuid), std::move(minidump));
}

std::vector<ReportId> Store::GetReports() const { return metadata_.Reports(); }

bool Store::Contains(const ReportId report_id) const { return metadata_.Contains(report_id); }

bool Store::Remove(const ReportId report_id) {
  if (!metadata_.Contains(report_id)) {
    return false;
  }

  //  The report is stored under /tmp/store/<program shortname>/$id.
  //  We first delete /tmp/store/<program shortname>/$id and then if $id was the only report
  //  for
  // <program shortname>, we also delete /tmp/store/<program name>.
  if (!DeletePath(metadata_.ReportDirectory(report_id))) {
    FX_LOGS(ERROR) << "Failed to delete report at " << metadata_.ReportDirectory(report_id);
  }

  // If this was the last report for a program, delete the directory for the program.
  const auto& program = metadata_.ReportProgram(report_id);
  if (metadata_.ProgramReports(program).size() == 1 &&
      !DeletePath(metadata_.ProgramDirectory(program))) {
    FX_LOGS(ERROR) << "Failed to delete " << metadata_.ProgramDirectory(program);
  }

  metadata_.Delete(report_id);

  return true;
}

void Store::RemoveAll() {
  if (!DeletePath(root_dir_)) {
    FX_LOGS(ERROR) << "Failed to delete all reports";
  }
  files::CreateDirectory(root_dir_);

  metadata_.RecreateFromFilesystem();
}

bool Store::MakeFreeSpace(const StorageSize required_space,
                          std::vector<ReportId>* garbage_collected_reports) {
  if (required_space >
      metadata_.CurrentSize() + metadata_.RemainingSpace() /*the store's max size*/) {
    return false;
  }

  FX_CHECK(garbage_collected_reports);
  garbage_collected_reports->clear();

  StorageSize remaining_space = metadata_.RemainingSpace();
  if (remaining_space > required_space) {
    return true;
  }

  // Reports will be garbage collected based on 1) how many reports their respective programs have
  // and 2) how old they are.
  struct GCMetadata {
    ReportId oldest_report;
    size_t num_remaining;
  };

  // Create the garbage collection metadata for the reports.
  std::deque<GCMetadata> gc_order;
  for (const auto& program : metadata_.Programs()) {
    const auto& report_ids = metadata_.ProgramReports(program);

    for (size_t i = 0; i < report_ids.size(); ++i) {
      gc_order.push_back(GCMetadata{
          .oldest_report = report_ids[i],
          .num_remaining = report_ids.size() - i,
      });
    }
  }

  // Sort |gc_order| such that the report at the front of the queue is the oldest report of the set
  // of programs with the largest number of reports.
  std::sort(gc_order.begin(), gc_order.end(), [](const GCMetadata& lhs, const GCMetadata& rhs) {
    if (lhs.num_remaining != rhs.num_remaining) {
      return lhs.num_remaining > rhs.num_remaining;
    } else {
      return lhs.oldest_report < rhs.oldest_report;
    }
  });

  size_t num_garbage_collected{0};
  const size_t total_num_report_ids = metadata_.Reports().size();

  // Commit to garbage collection reports until either all reports are garbage collected or
  // enough space has been freed.
  for (size_t i = 0; i < total_num_report_ids && remaining_space < required_space; ++i) {
    remaining_space -= metadata_.ReportSize(gc_order[i].oldest_report);
    ++num_garbage_collected;
  }

  // Remove reports.
  for (size_t i = 0; i < num_garbage_collected; ++i) {
    garbage_collected_reports->push_back(gc_order[i].oldest_report);
    Remove(gc_order[i].oldest_report);
  }
  info_.LogGarbageCollection(num_garbage_collected);

  return true;
}

}  // namespace crash_reports
}  // namespace forensics
