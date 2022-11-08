// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_store.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <set>
#include <vector>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/report_util.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

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

std::string FormatAnnotationsAsJson(const AnnotationMap& annotations) {
  rapidjson::Document json(rapidjson::kObjectType);
  auto& allocator = json.GetAllocator();

  for (const auto& [k, v] : annotations.Raw()) {
    auto key = rapidjson::Value(k, allocator);
    auto val = rapidjson::Value(v, allocator);
    json.AddMember(key, val, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

  json.Accept(writer);

  return buffer.GetString();
}

bool ReadAnnotations(const std::string& path, AnnotationMap* annotations) {
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

  // Empty annotations will be rejected by the crash server.
  if (local_annotations.empty()) {
    return false;
  }

  *annotations = AnnotationMap(std::move(local_annotations));
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

std::string ReadSnapshotUuid(const std::string& path) {
  SnapshotUuid snapshot_uuid;
  return (files::ReadFileToString(path, &snapshot_uuid)) ? snapshot_uuid : kNoUuidSnapshotUuid;
}

}  // namespace

ReportStore::ReportStore(LogTags* tags, std::shared_ptr<InfoContext> info,
                         feedback::AnnotationManager* annotation_manager,
                         const ReportStore::Root& temp_root,
                         const ReportStore::Root& persistent_root,
                         const std::string& garbage_collected_snapshots_path,
                         StorageSize max_archives_size)
    : tmp_metadata_(temp_root.dir, temp_root.max_size),
      cache_metadata_(persistent_root.dir, persistent_root.max_size),
      tags_(tags),
      info_(std::move(info)),
      snapshot_store_(annotation_manager, garbage_collected_snapshots_path, max_archives_size) {
  info_.LogMaxReportStoreSize(temp_root.max_size + persistent_root.max_size);

  // Clean up any empty directories in the report store. This may happen if the component stops
  // running while it is deleting a report.
  RemoveEmptyDirectories(tmp_metadata_.RootDir());
  RemoveEmptyDirectories(cache_metadata_.RootDir());

  // |temp_root.dir| must be usable immediately.
  FX_CHECK(RecreateFromFilesystem(tmp_metadata_));
  RecreateFromFilesystem(cache_metadata_);
}

bool ReportStore::Add(Report report, std::vector<ReportId>* garbage_collected_reports) {
  if (Contains(report.Id())) {
    FX_LOGST(ERROR, tags_->Get(report.Id())) << "Duplicate local report id";
    return false;
  }

  for (const auto& key : kReservedAttachmentNames) {
    if (report.Attachments().find(key) != report.Attachments().end()) {
      FX_LOGST(ERROR, tags_->Get(report.Id())) << "Attachment is using reserved key: " << key;
      return false;
    }
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

  auto& root_metadata = PickRootForStorage(report_size);

  return Add(report.Id(), report.ProgramShortname(), report_size, attachments, root_metadata,
             garbage_collected_reports);
}

bool ReportStore::Add(const ReportId report_id, const std::string& program_shortname,
                      const StorageSize report_size,
                      const std::map<std::string, SizedData>& attachments,
                      ReportStoreMetadata& store_root,
                      std::vector<ReportId>* garbage_collected_reports) {
  // Delete the persisted files and attempt to store the report under a new directory.
  auto on_error = [this, &store_root, report_id, program_shortname, report_size, &attachments,
                   garbage_collected_reports](const std::optional<std::string>& report_dir) {
    if (report_dir.has_value()) {
      DeletePath(*report_dir);
    }

    if (!HasFallbackRoot(store_root)) {
      return false;
    }

    auto& fallback_root = FallbackRoot(store_root);
    FX_LOGST(INFO, tags_->Get(report_id)) << "Using fallback root: " << fallback_root.RootDir();

    return Add(report_id, program_shortname, report_size, attachments, fallback_root,
               garbage_collected_reports);
  };

  // Ensure there's enough space in the store for the report.
  if (!MakeFreeSpace(store_root, report_size, garbage_collected_reports)) {
    FX_LOGST(ERROR, tags_->Get(report_id)) << "Failed to make space for report";
    return on_error(std::nullopt);
  }

  const std::string program_dir = files::JoinPath(store_root.RootDir(), program_shortname);
  const std::string report_dir = files::JoinPath(program_dir, std::to_string(report_id));

  if (!files::CreateDirectory(report_dir)) {
    FX_LOGST(ERROR, tags_->Get(report_id))
        << "Failed to create directory for report: " << report_dir;
    return on_error(std::nullopt);
  }

  std::vector<std::string> attachment_keys;
  for (const auto& [key, data] : attachments) {
    attachment_keys.push_back(key);

    // Write the report's content to the the filesystem.
    if (!WriteData(files::JoinPath(report_dir, key), data)) {
      FX_LOGST(ERROR, tags_->Get(report_id)) << "Failed to write attachment " << key;
      return on_error(report_dir);
    }
  }

  store_root.Add(report_id, program_shortname, std::move(attachment_keys), report_size);

  return true;
}

void ReportStore::AddAnnotation(ReportId id, const std::string& key, const std::string& value) {
  FX_CHECK(Contains(id)) << "Contains() should be called before any AddAnnotation()";

  auto& root_metadata = RootFor(id);
  const auto annotations_path = root_metadata.ReportAttachmentPath(id, "annotations.json");

  if (!annotations_path.has_value()) {
    FX_LOGST(ERROR, tags_->Get(id)) << "annotations.json doesn't exist";
    return;
  }

  AnnotationMap annotations;
  if (!ReadAnnotations(*annotations_path, &annotations)) {
    FX_LOGST(ERROR, tags_->Get(id)) << "Failed to read annotations.json";
    return;
  }

  if (annotations.Contains(key)) {
    FX_LOGST(FATAL, tags_->Get(id)) << "Annotation with key: '" << key << "' already exists";
  }

  annotations.Set(key, value);

  const std::string annotations_json = FormatAnnotationsAsJson(annotations);
  if (!WriteData(*annotations_path, annotations_json)) {
    FX_LOGST(ERROR, tags_->Get(id)) << "Failed to update annotations.json";
    return;
  }

  root_metadata.IncreaseSize(id, StorageSize::Bytes(key.size()));
  root_metadata.IncreaseSize(id, StorageSize::Bytes(value.size()));
}

Report ReportStore::Get(const ReportId report_id) {
  FX_CHECK(Contains(report_id)) << "Contains() should be called before any Get()";

  auto& root_metadata = RootFor(report_id);
  auto attachment_files = root_metadata.ReportAttachments(report_id, /*absolute_paths=*/
                                                          false);
  auto attachment_paths = root_metadata.ReportAttachments(report_id, /*absolute_paths=*/true);

  AnnotationMap annotations;
  std::map<std::string, SizedData> attachments;
  SnapshotUuid snapshot_uuid;
  std::optional<SizedData> minidump;

  for (size_t i = 0; i < attachment_files.size(); ++i) {
    if (attachment_files[i] == "annotations.json") {
      if (!ReadAnnotations(attachment_paths[i], &annotations)) {
        continue;
      }
    } else if (attachment_files[i] == "snapshot_uuid.txt") {
      snapshot_uuid = ReadSnapshotUuid(attachment_paths[i]);
    } else {
      SizedData attachment;
      if (!ReadAttachment(attachment_paths[i], &attachment)) {
        continue;
      }

      if (attachment_files[i] == "minidump.dmp") {
        minidump = std::move(attachment);
      } else {
        attachments.emplace(attachment_files[i], std::move(attachment));
      }
    }
  }

  return Report(report_id, root_metadata.ReportProgram(report_id), std::move(annotations),
                std::move(attachments), std::move(snapshot_uuid), std::move(minidump));
}

std::vector<ReportId> ReportStore::GetReports() const {
  auto all_reports = tmp_metadata_.Reports();
  const auto cache_reports = cache_metadata_.Reports();
  all_reports.insert(all_reports.end(), cache_reports.begin(), cache_reports.end());

  return all_reports;
}

SnapshotUuid ReportStore::GetSnapshotUuid(const ReportId id) {
  if (!Contains(id)) {
    return kNoUuidSnapshotUuid;
  }

  auto& root_metadata = RootFor(id);
  auto attachment_files = root_metadata.ReportAttachments(id, /*absolute_paths=*/
                                                          false);
  auto attachment_paths = root_metadata.ReportAttachments(id, /*absolute_paths=*/true);

  std::string snapshot_uuid;
  for (size_t i = 0; i < attachment_files.size(); ++i) {
    if (attachment_files[i] != kSnapshotUuidFilename) {
      continue;
    }

    return ReadSnapshotUuid(attachment_paths[i]);
  }

  // This should not happen as we always expect a kSnapshotUuidFilename file to exist.
  return kNoUuidSnapshotUuid;
}

bool ReportStore::Contains(const ReportId report_id) {
  // Keep the in-memory and on-disk knowledge of the store in sync in case the
  // filesystem has deleted the report content. This is done here because it is a natural
  // synchronization point and any operation acting on a report must call Contains in order to
  // safely proceed.
  if (tmp_metadata_.Contains(report_id) &&
      !files::IsDirectory(tmp_metadata_.ReportDirectory(report_id))) {
    tmp_metadata_.Delete(report_id);
  }

  if (cache_metadata_.Contains(report_id) &&
      !files::IsDirectory(cache_metadata_.ReportDirectory(report_id))) {
    cache_metadata_.Delete(report_id);
  }

  return tmp_metadata_.Contains(report_id) || cache_metadata_.Contains(report_id);
}

bool ReportStore::Remove(const ReportId report_id) {
  if (!Contains(report_id)) {
    return false;
  }

  auto& root_metadata = RootFor(report_id);

  //  The report is stored under /{cache, tmp}/store/<program shortname>/$id.
  //  We first delete /tmp/store/<program shortname>/$id and then if $id was the only report
  //  for <program shortname>, we also delete /{cache, tmp}/store/<program name>.
  if (!DeletePath(root_metadata.ReportDirectory(report_id))) {
    FX_LOGST(ERROR, tags_->Get(report_id))
        << "Failed to delete report at " << root_metadata.ReportDirectory(report_id);
  }

  // If this was the last report for a program, delete the directory for the program.
  const auto& program = root_metadata.ReportProgram(report_id);
  if (root_metadata.ProgramReports(program).size() == 1 &&
      !DeletePath(root_metadata.ProgramDirectory(program))) {
    FX_LOGST(ERROR, tags_->Get(report_id))
        << "Failed to delete " << root_metadata.ProgramDirectory(program);
  }

  root_metadata.Delete(report_id);

  return true;
}

void ReportStore::RemoveAll() {
  auto DeleteAll = [](const std::string& root_dir) {
    if (!DeletePath(root_dir)) {
      FX_LOGS(ERROR) << "Failed to delete all reports from " << root_dir;
    }
    files::CreateDirectory(root_dir);
  };

  DeleteAll(tmp_metadata_.RootDir());

  FX_CHECK(RecreateFromFilesystem(tmp_metadata_));

  if (cache_metadata_.IsDirectoryUsable()) {
    DeleteAll(cache_metadata_.RootDir());
  }
  RecreateFromFilesystem(cache_metadata_);
}

bool ReportStore::RecreateFromFilesystem(ReportStoreMetadata& store_root) {
  const bool success = store_root.RecreateFromFilesystem();
  for (const auto report_id : store_root.Reports()) {
    tags_->Register(report_id, {Logname(store_root.ReportProgram(report_id))});
  }

  return success;
}

ReportStoreMetadata& ReportStore::RootFor(const ReportId id) {
  if (tmp_metadata_.Contains(id)) {
    return tmp_metadata_;
  }

  if (!cache_metadata_.IsDirectoryUsable()) {
    FX_LOGS(FATAL) << "Unable to find root for " << id << ", there's a logic bug somewhere";
  }

  return cache_metadata_;
}

ReportStoreMetadata& ReportStore::PickRootForStorage(const StorageSize report_size) {
  // Attempt to make |cache_metadata_| usable if it isn't already.
  if (!cache_metadata_.IsDirectoryUsable()) {
    RecreateFromFilesystem(cache_metadata_);
  }

  // Only use cache if it's valid and there's enough space to put the report there.
  return (!cache_metadata_.IsDirectoryUsable() || cache_metadata_.RemainingSpace() < report_size)
             ? tmp_metadata_
             : cache_metadata_;
}

bool ReportStore::HasFallbackRoot(const ReportStoreMetadata& store_root) {
  // Only /cache can fallback.
  return &store_root == &cache_metadata_;
}

ReportStoreMetadata& ReportStore::FallbackRoot(ReportStoreMetadata& store_root) {
  FX_CHECK(HasFallbackRoot(store_root));
  // Always fallback to /tmp.
  return tmp_metadata_;
}

bool ReportStore::MakeFreeSpace(const ReportStoreMetadata& root_metadata,
                                const StorageSize required_space,
                                std::vector<ReportId>* garbage_collected_reports) {
  if (required_space >
      root_metadata.CurrentSize() + root_metadata.RemainingSpace() /*the store's max size*/) {
    return false;
  }

  FX_CHECK(garbage_collected_reports);
  garbage_collected_reports->clear();

  StorageSize remaining_space = root_metadata.RemainingSpace();
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
  for (const auto& program : root_metadata.Programs()) {
    const auto& report_ids = root_metadata.ProgramReports(program);

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
  const size_t total_num_report_ids = root_metadata.Reports().size();

  // Commit to garbage collection reports until either all reports are garbage collected or
  // enough space has been freed.
  for (size_t i = 0; i < total_num_report_ids && remaining_space < required_space; ++i) {
    remaining_space += root_metadata.ReportSize(gc_order[i].oldest_report);
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

SnapshotStore* ReportStore::GetSnapshotStore() { return &snapshot_store_; }

}  // namespace crash_reports
}  // namespace forensics
