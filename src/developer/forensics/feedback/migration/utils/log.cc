// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/log.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"

namespace forensics::feedback {
namespace {

std::string Serialize(const MigrationLog::Component component) {
  switch (component) {
    case MigrationLog::Component::kLastReboot:
      return "last_reboot";
    case MigrationLog::Component::kCrashReports:
      return "crash_reports";
    case MigrationLog::Component::kFeedbackData:
      return "feedback_data";
  }
}

std::optional<MigrationLog::Component> DeserializeComponent(const std::string& raw_component) {
  if (raw_component == "last_reboot") {
    return MigrationLog::Component::kLastReboot;
  }

  if (raw_component == "crash_reports") {
    return MigrationLog::Component::kCrashReports;
  }

  if (raw_component == "feedback_data") {
    return MigrationLog::Component::kFeedbackData;
  }

  return std::nullopt;
}

std::string Serialize(const std::set<MigrationLog::Component> components) {
  using namespace rapidjson;

  Document doc;
  doc.SetObject();

  Value migrated(kArrayType);
  auto& allocator = doc.GetAllocator();
  for (const auto& component : components) {
    migrated.PushBack(Value(Serialize(component), allocator), allocator);
  }

  doc.AddMember("migrated", migrated, allocator);

  StringBuffer buffer;
  PrettyWriter<StringBuffer> writer(buffer);

  doc.Accept(writer);

  return buffer.GetString();
}

std::optional<std::set<MigrationLog::Component>> DeserializeLog(const std::string& raw_log) {
  using namespace rapidjson;

  std::set<MigrationLog::Component> migrated;
  if (raw_log.empty()) {
    return migrated;
  }

  Document doc;
  ParseResult ok = doc.Parse(raw_log);
  if (!ok) {
    FX_LOGS(ERROR) << "Error parsing migration log as JSON at offset " << ok.Offset() << " "
                   << GetParseError_En(ok.Code());
    return std::nullopt;
  }

  if (!doc.IsObject()) {
    FX_LOGS(ERROR) << "Migration log is not a JSON object";
    return std::nullopt;
  }

  if (!doc.HasMember("migrated") || !doc["migrated"].IsArray()) {
    FX_LOGS(ERROR) << "Migration log doesn't have migrated array";
    return std::nullopt;
  }

  for (const auto& elem : doc["migrated"].GetArray()) {
    if (!elem.IsString()) {
      FX_LOGS(ERROR) << "Array element is not a string, skipping";
      continue;
    }

    if (const auto component = DeserializeComponent(elem.GetString()); component) {
      migrated.insert(*component);
    } else {
      FX_LOGS(ERROR) << "Failed to deserialize " << elem.GetString();
    }
  }

  return migrated;
}

}  // namespace

std::optional<MigrationLog> MigrationLog::FromFile(const std::string path) {
  if (!files::IsFile(path) && !files::WriteFile(path, "")) {
    FX_LOGS(ERROR) << "Failed to create backing file for the migration log";
    return std::nullopt;
  }

  std::string raw_log;
  if (!files::ReadFileToString(path, &raw_log)) {
    FX_LOGS(WARNING) << "Failed to read existing migration log";
    return std::nullopt;
  }

  auto migrated = DeserializeLog(raw_log);
  if (!migrated) {
    FX_LOGS(ERROR) << "Failed to deserialize migration log";
    return std::nullopt;
  }

  return MigrationLog(path, std::move(*migrated));
}

MigrationLog::MigrationLog(std::string path, std::set<MigrationLog::Component> migrated)
    : path_(std::move(path)), migrated_(std::move(migrated)) {}

bool MigrationLog::Contains(Component component) const { return migrated_.count(component); }

void MigrationLog::Set(const Component component) {
  migrated_.insert(component);

  if (!files::WriteFile(path_, Serialize(migrated_))) {
    FX_LOGS(ERROR) << "Failed to update migration log, setting " << Serialize(component)
                   << " not persisted";
  }
}

}  // namespace forensics::feedback
