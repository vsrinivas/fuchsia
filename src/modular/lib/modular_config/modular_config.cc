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

ModularConfigReader::ModularConfigReader(fbl::unique_fd dir_fd) {
  FX_CHECK(dir_fd.get() >= 0);

  // 1. Figure out where the config file is.
  std::string config_path = files::JoinPath(StripLeadingSlash(modular_config::kOverriddenConfigDir),
                                            modular_config::kStartupConfigFilePath);
  if (!files::IsFileAt(dir_fd.get(), config_path)) {
    config_path = files::JoinPath(StripLeadingSlash(modular_config::kDefaultConfigDir),
                                  modular_config::kStartupConfigFilePath);
  }

  // 2. Read the file
  std::string config;
  if (!files::ReadFileToStringAt(dir_fd.get(), config_path, &config)) {
    FX_LOGS(ERROR) << "Failed to read file: " << config_path;
    UseDefaults();
    return;
  }

  // 3. Parse the JSON
  ParseConfig(config, config_path);
}

ModularConfigReader::ModularConfigReader(std::string config) {
  constexpr char kPathForErrorStrings[] = ".";

  ParseConfig(config, kPathForErrorStrings);
}

// static
ModularConfigReader ModularConfigReader::CreateFromNamespace() {
  return ModularConfigReader(fbl::unique_fd(open("/", O_RDONLY)));
}

// static
std::string ModularConfigReader::GetOverriddenConfigPath() {
  return files::JoinPath(StripLeadingSlash(modular_config::kOverriddenConfigDir),
                         modular_config::kStartupConfigFilePath);
}

// static
bool ModularConfigReader::OverriddenConfigExists() {
  return files::IsFile(GetOverriddenConfigPath());
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

}  // namespace modular
