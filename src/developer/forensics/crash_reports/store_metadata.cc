// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/store_metadata.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

namespace forensics {
namespace crash_reports {

namespace fs = std::filesystem;

StoreMetadata::StoreMetadata(std::string store_root, const StorageSize max_size)
    : store_root_(std::move(store_root)),
      max_size_(max_size),
      current_size_(StorageSize::Bytes(0u)) {
  RecreateFromFilesystem();
}

void StoreMetadata::RecreateFromFilesystem() {
  current_size_ = StorageSize::Bytes(0u);
  report_metadata_.clear();
  program_metadata_.clear();

  if (!fs::is_directory(store_root_)) {
    FX_CHECK(fs::create_directory(store_root_));
  }

  for (const auto& program_dir : fs::directory_iterator(store_root_)) {
    const std::string program = program_dir.path().filename();

    for (const auto& report_dir : fs::directory_iterator(program_dir)) {
      const ReportId report_id = std::stoull(report_dir.path().filename());

      std::vector<std::string> attachments;
      StorageSize report_size = StorageSize::Bytes(0);
      for (const auto& attachment : fs::directory_iterator(report_dir)) {
        attachments.push_back(attachment.path().filename());
        std::error_code ec;
        StorageSize attachment_size = StorageSize::Bytes(fs::file_size(attachment, ec));

        if (!ec) {
          report_size += attachment_size;
        }
      }

      current_size_ += report_size;

      report_metadata_[report_id].size = report_size;
      report_metadata_[report_id].dir = report_dir.path();
      report_metadata_[report_id].program = program;
      report_metadata_[report_id].attachments = std::move(attachments);

      program_metadata_[program].dir = program_dir.path();
      program_metadata_[program].report_ids.push_back(report_id);
    }
  }

  // Sort the reports such that the oldest report is at the front of the queue.
  for (auto& [_, metadata] : program_metadata_) {
    std::sort(metadata.report_ids.begin(), metadata.report_ids.end());
  }
}

bool StoreMetadata::Contains(const ReportId report_id) const {
  return report_metadata_.find(report_id) != report_metadata_.end();
}

bool StoreMetadata::Contains(const std::string& program) const {
  return program_metadata_.find(program) != program_metadata_.end();
}

StorageSize StoreMetadata::CurrentSize() const { return current_size_; }

StorageSize StoreMetadata::RemainingSpace() const { return max_size_ - current_size_; }

void StoreMetadata::Add(const ReportId report_id, std::string program,
                        std::vector<std::string> attachments, const StorageSize size) {
  current_size_ += size;

  program_metadata_[program].dir = fs::path(store_root_) / program;
  program_metadata_[program].report_ids.push_back(report_id);

  report_metadata_[report_id].size = size;
  report_metadata_[report_id].dir =
      fs::path(program_metadata_[program].dir) / std::to_string(report_id);
  report_metadata_[report_id].program = std::move(program);
  report_metadata_[report_id].attachments = std::move(attachments);
}

void StoreMetadata::Delete(const ReportId report_id) {
  FX_CHECK(Contains(report_id));

  const auto& program = ReportProgram(report_id);
  auto& report_ids = program_metadata_[program].report_ids;
  report_ids.erase(std::find(report_ids.begin(), report_ids.end(), report_id));

  current_size_ -= report_metadata_[report_id].size;
  if (report_ids.empty()) {
    program_metadata_.erase(program);
  }
  report_metadata_.erase(report_id);
}

std::vector<std::string> StoreMetadata::Programs() const {
  std::vector<std::string> programs;
  for (const auto& [program, _] : program_metadata_) {
    programs.push_back(program);
  }

  return programs;
}

std::vector<ReportId> StoreMetadata::Reports() const {
  std::vector<ReportId> report_ids;
  for (const auto& [report_id, _] : report_metadata_) {
    report_ids.push_back(report_id);
  }

  return report_ids;
}

const std::deque<ReportId>& StoreMetadata::ProgramReports(const std::string& program) {
  FX_CHECK(program_metadata_.find(program) != program_metadata_.end());
  return program_metadata_[program].report_ids;
}

const std::string& StoreMetadata::ReportProgram(const ReportId report_id) {
  FX_CHECK(Contains(report_id));
  return report_metadata_[report_id].program;
}

const std::string& StoreMetadata::ProgramDirectory(const std::string& program) {
  FX_CHECK(program_metadata_.find(program) != program_metadata_.end());
  return program_metadata_[program].dir;
}

const std::string& StoreMetadata::ReportDirectory(const ReportId report_id) {
  FX_CHECK(Contains(report_id));
  return report_metadata_[report_id].dir;
}

StorageSize StoreMetadata::ReportSize(const ReportId report_id) {
  FX_CHECK(Contains(report_id));
  return report_metadata_[report_id].size;
}

std::vector<std::string> StoreMetadata::ReportAttachments(ReportId report_id,
                                                          const bool absolute_paths) {
  FX_CHECK(Contains(report_id));

  auto& report_metadata = report_metadata_[report_id];
  if (!absolute_paths) {
    return report_metadata.attachments;
  }

  std::vector<std::string> attachments;
  attachments.reserve(report_metadata.attachments.size());
  for (const auto& attachment : report_metadata.attachments) {
    attachments.push_back(fs::path(report_metadata.dir) / attachment);
  }

  return attachments;
}

}  // namespace crash_reports
}  // namespace forensics
