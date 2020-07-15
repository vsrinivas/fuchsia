// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/store.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include <set>
#include <vector>

#include "src/developer/forensics/utils/sized_data.h"
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

const std::set<std::string> kReservedAttachmentNames = {
    kAnnotationsFilename,
    kMinidumpFilename,
};

// Join |paths| in order into a single path.
std::string JoinPaths(const std::vector<std::string>& paths) {
  return fxl::JoinStrings(paths, "/");
}

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

bool WriteAttachment(const std::string& path, const SizedData& attachment) {
  return files::WriteFile(path, reinterpret_cast<const char*>(attachment.data()),
                          attachment.size());
}

bool ReadAttachment(const std::string& path, SizedData* attachment) {
  return files::ReadFileToVector(path, attachment);
}

}  // namespace

Store::Store(std::shared_ptr<InfoContext> info, const std::string& root_dir, StorageSize max_size)
    : root_dir_(root_dir), max_size_(max_size), current_size_(0u), info_(std::move(info)) {
  info_.LogMaxStoreSize(max_size_);

  // Clean up any empty directories under |root_dir_|. This may happen if the component stops
  // running while it is deleting a report.
  RemoveEmptyDirectories(root_dir_);

  RebuildMetadata();
}

void Store::RebuildMetadata() {
  // Rebuild the store's metadata by iterating through each reports filed and determining its
  // Uid, size, location in the filesystem, and program shortname. Additionally, determine what the
  // id of the next report filed should be incrementing the maximum report id found by 1.
  for (const auto& program_shortname : GetDirectoryContents(root_dir_)) {
    const auto report_ids = GetDirectoryContents(files::JoinPath(root_dir_, program_shortname));
    for (const auto& id_str : report_ids) {
      const Uid id = std::stoull(id_str);
      const std::string dir = JoinPaths({root_dir_, program_shortname, id_str});

      // Get the size of the files in the report.
      StorageSize size = StorageSize::Bytes(0);
      for (const auto& filename : GetDirectoryContents(dir)) {
        size_t file_size{0};
        files::GetFileSize(files::JoinPath(dir, filename), &file_size);
        size += StorageSize::Bytes(file_size);
      }

      id_to_metadata_[id] = ReportMetadata{
          .dir = dir,
          .size = size,
          .program_shortname = program_shortname,
      };

      current_size_ += size;
      uids_.push_back(id);
    }
  }

  std::sort(uids_.begin(), uids_.end());
  if (!uids_.empty()) {
    next_id_ = uids_.back() + 1;
  }
}

std::optional<Store::Uid> Store::Add(const Report report) {
  for (const auto& key : kReservedAttachmentNames) {
    if (report.Attachments().find(key) != report.Attachments().end()) {
      FX_LOGS(ERROR) << "Attachment is using reserved key: " << key;
      return std::nullopt;
    }
  }

  const Uid id = next_id_++;
  const std::string dir = JoinPaths({root_dir_, report.ProgramShortname(), std::to_string(id)});

  auto cleanup_on_error = fit::defer([dir] { DeletePath(dir); });

  if (!files::CreateDirectory(dir)) {
    FX_LOGS(ERROR) << "Failed to create directory for report: " << dir;
    return std::nullopt;
  }

  const std::string annotations_json = FormatAnnotationsAsJson(report.Annotations());

  // Determine the size of the report.
  StorageSize report_size = StorageSize::Bytes(annotations_json.size());
  for (const auto& [_, v] : report.Attachments()) {
    report_size += StorageSize::Bytes(v.size());
  }
  if (report.Minidump().has_value()) {
    report_size += StorageSize::Bytes(report.Minidump().value().size());
  }

  // Ensure there's enough space in the store for the report.
  if (!MakeFreeSpace(report_size)) {
    FX_LOGS(ERROR) << "Failed to make space for report";
    return std::nullopt;
  }

  auto MakeFilepath = [dir](const std::string& filename) { return files::JoinPath(dir, filename); };

  // Write the report's content to the the filesystem.
  if (!files::WriteFile(MakeFilepath(kAnnotationsFilename), annotations_json)) {
    FX_LOGS(ERROR) << "Failed to write annotations";
    return std::nullopt;
  }
  for (const auto& [filename, attachment] : report.Attachments()) {
    if (!WriteAttachment(MakeFilepath(filename), attachment)) {
      FX_LOGS(ERROR) << "Failed to write attachment " << filename;
      return std::nullopt;
    }
  }
  if (report.Minidump().has_value()) {
    if (!WriteAttachment(MakeFilepath(kMinidumpFilename), report.Minidump().value())) {
      FX_LOGS(ERROR) << "Failed to write minidump";
      return std::nullopt;
    }
  }

  id_to_metadata_[id] = ReportMetadata{
      .dir = dir,
      .size = report_size,
      .program_shortname = report.ProgramShortname(),
  };
  uids_.push_back(id);
  current_size_ += report_size;

  cleanup_on_error.cancel();
  return id;
}

std::optional<Report> Store::Get(const Store::Uid& id) {
  if (!Contains(id)) {
    return std::nullopt;
  }

  const std::vector<std::string> report_files = GetDirectoryContents(id_to_metadata_[id].dir);

  if (report_files.empty()) {
    return std::nullopt;
  }

  auto MakeFilepath = [dir = id_to_metadata_[id].dir](const std::string& filename) {
    return files::JoinPath(dir, filename);
  };

  std::map<std::string, std::string> annotations;
  std::map<std::string, SizedData> attachments;
  std::optional<SizedData> minidump;

  for (const auto& filename : report_files) {
    if (filename == "annotations.json") {
      if (!ReadAnnotations(MakeFilepath(filename), &annotations)) {
        return std::nullopt;
      }
    } else {
      SizedData attachment;
      if (!ReadAttachment(MakeFilepath(filename), &attachment)) {
        return std::nullopt;
      }

      if (filename == "minidump.dmp") {
        minidump = std::move(attachment);
      } else {
        attachments.emplace(filename, std::move(attachment));
      }
    }
  }

  return Report(id_to_metadata_[id].program_shortname, std::move(annotations),
                std::move(attachments), std::move(minidump));
}

bool Store::Contains(const Uid& id) const {
  return id_to_metadata_.find(id) != id_to_metadata_.end();
}

bool Store::Remove(const Uid& id) {
  if (!Contains(id)) {
    return false;
  }

  //  The report is stored under /tmp/store/<program shortname>/$id.
  //  We first delete /tmp/store/<program shortname>/$id and then if $id was the only report for
  // <program shortname>, we also delete /tmp/store/<program name>.
  if (!DeletePath(id_to_metadata_.at(id).dir)) {
    FX_LOGS(ERROR) << "Failed to delete report at " << id_to_metadata_.at(id).dir;
  }

  const std::string program_path =
      files::JoinPath(root_dir_, id_to_metadata_.at(id).program_shortname);
  const std::vector<std::string> dir_contents = GetDirectoryContents(program_path);
  if (dir_contents.empty()) {
    if (!DeletePath(program_path)) {
      FX_LOGS(ERROR) << "Failed to delete " << program_path;
    }
  }

  current_size_ -= id_to_metadata_[id].size;
  id_to_metadata_.erase(id);
  uids_.erase(std::remove(uids_.begin(), uids_.end(), id), uids_.end());

  return true;
}

void Store::RemoveAll() {
  if (!DeletePath(root_dir_)) {
    FX_LOGS(ERROR) << "Failed to delete all reports";
  }
  files::CreateDirectory(root_dir_);

  current_size_ = StorageSize::Bytes(0u);
  id_to_metadata_.clear();
  uids_.clear();
}

bool Store::MakeFreeSpace(const StorageSize required_space) {
  if (required_space > max_size_) {
    return false;
  }

  size_t num_garbage_collected{0};
  while ((current_size_ + required_space) > max_size_ && !uids_.empty()) {
    if (Remove(uids_.front())) {
      ++num_garbage_collected;
    }
  }
  info_.LogGarbageCollection(num_garbage_collected);

  return true;
}

}  // namespace crash_reports
}  // namespace forensics
