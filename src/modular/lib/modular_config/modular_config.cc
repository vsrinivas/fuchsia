// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_config/modular_config_xdr.h"

// Flags passed to RapidJSON that control JSON parsing behavior.
// This is used to enable parsing non-standard JSON syntax, like comments.
constexpr unsigned kModularConfigParseFlags = rapidjson::kParseCommentsFlag;

namespace modular {
namespace {

rapidjson::Document GetSectionAsDoc(const rapidjson::Document& doc,
                                    const std::string& section_name) {
  rapidjson::Document section_doc;

  auto config_json = doc.FindMember(section_name);
  if (config_json != doc.MemberEnd()) {
    section_doc.CopyFrom(config_json->value, section_doc.GetAllocator());
  } else {
    // |section_name| was not found; return an empty object
    section_doc.SetObject();
  }

  return section_doc;
}

std::string StripLeadingSlash(std::string str) {
  if (str.find("/") == 0)
    return str.substr(1);
  return str;
}

}  // namespace

fpromise::result<fuchsia::modular::session::ModularConfig, std::string> ParseConfig(
    std::string_view config_json) {
  rapidjson::Document doc;

  doc.Parse<kModularConfigParseFlags>(config_json.data(), config_json.length());
  if (doc.HasParseError()) {
    auto error = std::stringstream();
    error << "Failed to parse JSON: " << rapidjson::GetParseError_En(doc.GetParseError()) << " ("
          << doc.GetErrorOffset() << ")";
    return fpromise::error(error.str());
  }

  fuchsia::modular::session::ModularConfig config;
  if (!XdrRead(&doc, &config, XdrModularConfig)) {
    return fpromise::error(
        "Failed to read JSON as Modular configuration (does not follow schema?)");
  }

  return fpromise::ok(std::move(config));
}

// Returns the default Modular configuration.
fuchsia::modular::session::ModularConfig DefaultConfig() {
  rapidjson::Document doc;
  doc.SetObject();

  fuchsia::modular::session::ModularConfig config;
  auto ok = XdrRead(&doc, &config, XdrModularConfig);
  FX_DCHECK(ok);

  return config;
}

std::string ConfigToJsonString(const fuchsia::modular::session::ModularConfig& config) {
  std::string json;
  auto config_copy = CloneStruct(config);
  XdrWrite(&json, &config_copy, XdrModularConfig);
  return json;
}

ModularConfigReader::ModularConfigReader(fbl::unique_fd root_dir) : root_dir_(std::move(root_dir)) {
  FX_CHECK(root_dir_.is_valid());

  // 1. Figure out where the config file is.
  std::string config_path = GetConfigDataConfigPath();
  if (OverriddenConfigExists()) {
    config_path = GetOverriddenConfigPath();
  } else if (PersistentConfigOverrideAllowed() && PersistentConfigExists()) {
    config_path = GetPersistentConfigPath();
  } else if (PackagedConfigExists()) {
    config_path = GetPackagedConfigPath();
  }

  FX_LOGS(INFO) << "Reading configuration from /" << config_path;

  // 2. Read the file
  std::string config;
  if (!files::ReadFileToStringAt(root_dir_.get(), config_path, &config)) {
    FX_LOGS(ERROR) << "Failed to read file: " << config_path;
    UseDefaults();
    return;
  }

  // 3. Parse the JSON
  ParseConfig(config, config_path);
}

// static
ModularConfigReader ModularConfigReader::CreateFromNamespace() {
  return ModularConfigReader(fbl::unique_fd(open("/", O_RDONLY)));
}

// static
std::string ModularConfigReader::GetConfigDataConfigPath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kConfigDataDir),
                         modular_config::kStartupConfigFilePath);
}

// static
std::string ModularConfigReader::GetOverriddenConfigPath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kOverriddenConfigDir),
                         modular_config::kStartupConfigFilePath);
}

// static
std::string ModularConfigReader::GetPersistentConfigPath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kPersistentConfigDir),
                         modular_config::kStartupConfigFilePath);
}

// static
std::string ModularConfigReader::GetPackagedConfigPath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kPackageDataDir),
                         modular_config::kStartupConfigFilePath);
}

// static
std::string ModularConfigReader::GetAllowPersistentConfigOverridePath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kConfigDataDir),
                         modular_config::kAllowPersistentConfigOverrideFilePath);
}

bool ModularConfigReader::OverriddenConfigExists() {
  return files::IsFileAt(root_dir_.get(), GetOverriddenConfigPath());
}

bool ModularConfigReader::PersistentConfigExists() {
  return files::IsFileAt(root_dir_.get(), GetPersistentConfigPath());
}

bool ModularConfigReader::PackagedConfigExists() {
  return files::IsFileAt(root_dir_.get(), GetPackagedConfigPath());
}

bool ModularConfigReader::PersistentConfigOverrideAllowed() {
  return files::IsFileAt(root_dir_.get(), GetAllowPersistentConfigOverridePath());
}

void ModularConfigReader::ParseConfig(const std::string& config, const std::string& config_path) {
  rapidjson::Document doc;

  doc.Parse<kModularConfigParseFlags>(config);
  if (doc.HasParseError()) {
    FX_LOGS(ERROR) << "Failed to parse " << config_path << ": "
                   << rapidjson::GetParseError_En(doc.GetParseError()) << " ("
                   << doc.GetErrorOffset() << ")";
    UseDefaults();
    return;
  }

  // Parse the `basemgr` and `sessionmgr` sections out of the config.
  rapidjson::Document basemgr_doc = GetSectionAsDoc(doc, modular_config::kBasemgrConfigName);
  rapidjson::Document sessionmgr_doc = GetSectionAsDoc(doc, modular_config::kSessionmgrConfigName);
  if (!XdrRead(&basemgr_doc, &basemgr_config_, XdrBasemgrConfig)) {
    FX_LOGS(ERROR) << "Unable to parse 'basemgr' from " << config_path;
  }
  if (!XdrRead(&sessionmgr_doc, &sessionmgr_config_, XdrSessionmgrConfig)) {
    FX_LOGS(ERROR) << "Unable to parse 'sessionmgr' from " << config_path;
  }
}

void ModularConfigReader::UseDefaults() {
  rapidjson::Document doc;
  doc.SetObject();
  XdrRead(&doc, &basemgr_config_, XdrBasemgrConfig);
  XdrRead(&doc, &sessionmgr_config_, XdrSessionmgrConfig);
}

fuchsia::modular::session::BasemgrConfig ModularConfigReader::GetBasemgrConfig() const {
  fuchsia::modular::session::BasemgrConfig result;
  basemgr_config_.Clone(&result);
  return result;
}

fuchsia::modular::session::SessionmgrConfig ModularConfigReader::GetSessionmgrConfig() const {
  fuchsia::modular::session::SessionmgrConfig result;
  sessionmgr_config_.Clone(&result);
  return result;
}

fuchsia::modular::session::ModularConfig ModularConfigReader::GetConfig() const {
  fuchsia::modular::session::ModularConfig result;
  basemgr_config_.Clone(result.mutable_basemgr_config());
  sessionmgr_config_.Clone(result.mutable_sessionmgr_config());
  return result;
}

// static
std::string ModularConfigReader::GetConfigAsString(
    fuchsia::modular::session::BasemgrConfig* basemgr_config,
    fuchsia::modular::session::SessionmgrConfig* sessionmgr_config) {
  std::string basemgr_json;
  std::string sessionmgr_json;
  XdrWrite(&basemgr_json, basemgr_config, XdrBasemgrConfig);
  XdrWrite(&sessionmgr_json, sessionmgr_config, XdrSessionmgrConfig);

  return fxl::Substitute(R"({
      "$0": $1,
      "$2": $3
    })",
                         modular_config::kBasemgrConfigName, basemgr_json,
                         modular_config::kSessionmgrConfigName, sessionmgr_json);
}

fpromise::result<fuchsia::modular::session::ModularConfig, std::string>
ModularConfigReader::ReadAndMaybePersistConfig(ModularConfigWriter* config_writer) {
  FX_DCHECK(config_writer);

  auto config = GetConfig();

  // Persist |config| if allowed and if the config was read from /config_override.
  if (modular::ModularConfigReader::PersistentConfigOverrideAllowed() &&
      modular::ModularConfigReader::OverriddenConfigExists()) {
    if (auto result = config_writer->Write(config); result.is_error()) {
      return fpromise::error("Failed to persist config_override: " + result.take_error());
    }
    FX_LOGS(INFO) << "Configuration from config_override has been persisted.";
  }

  return fpromise::ok(std::move(config));
}

ModularConfigWriter::ModularConfigWriter(fbl::unique_fd root_dir) : root_dir_(std::move(root_dir)) {
  FX_CHECK(root_dir_.is_valid());
}

ModularConfigWriter ModularConfigWriter::CreateFromNamespace() {
  return ModularConfigWriter(fbl::unique_fd(open(modular_config::kPersistentConfigDir, O_RDONLY)));
}

fpromise::result<void, std::string> ModularConfigWriter::Write(
    const fuchsia::modular::session::ModularConfig& config) {
  auto config_json = ConfigToJsonString(config);
  if (!files::WriteFileAt(root_dir_.get(), modular_config::kStartupConfigFilePath,
                          config_json.data(), config_json.size())) {
    return fpromise::error("could not write config file");
  }

  return fpromise::ok();
}

fpromise::result<void, std::string> ModularConfigWriter::Delete() {
  if (!files::IsFileAt(root_dir_.get(), modular_config::kStartupConfigFilePath)) {
    return fpromise::ok();
  }

  if (!files::DeletePathAt(root_dir_.get(), modular_config::kStartupConfigFilePath,
                           /*recursive=*/false)) {
    return fpromise::error("could not delete config file");
  }

  return fpromise::ok();
}

}  // namespace modular
