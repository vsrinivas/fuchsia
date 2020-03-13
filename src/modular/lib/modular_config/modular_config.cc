// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_config/modular_config.h"

#include <fcntl.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/json_parser/pretty_print.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/modular_config/modular_config_xdr.h"

namespace modular {
namespace {
std::string GetSectionAsString(const rapidjson::Document& doc, const std::string& section_name) {
  auto config_json = doc.FindMember(section_name);
  if (config_json == doc.MemberEnd()) {
    // |section_name| was not found
    return "{}";
  }

  return json_parser::JsonValueToString(config_json->value);
}

std::string StripLeadingSlash(std::string str) {
  if (str.find("/") == 0)
    return str.substr(1);
  return str;
}

}  // namespace

ModularConfigReader::ModularConfigReader(fbl::unique_fd dir_fd) {
  FX_CHECK(dir_fd.get() >= 0);

  // 1.  Figure out where the config file is.
  std::string config_path = files::JoinPath(StripLeadingSlash(modular_config::kOverriddenConfigDir),
                                            modular_config::kStartupConfigFilePath);
  if (!files::IsFileAt(dir_fd.get(), config_path)) {
    config_path = files::JoinPath(StripLeadingSlash(modular_config::kDefaultConfigDir),
                                  modular_config::kStartupConfigFilePath);
  }

  // 2. Parse the JSON out of the config file.
  json::JSONParser json_parser;
  auto doc = json_parser.ParseFromFileAt(dir_fd.get(), config_path);

  ParseConfig(std::move(json_parser), std::move(doc), config_path);
}

ModularConfigReader::ModularConfigReader(std::string config) {
  constexpr char kPathForErrorStrings[] = ".";
  // Parse the JSON out of the config string.
  json::JSONParser json_parser;
  auto doc = json_parser.ParseFromString(config, kPathForErrorStrings);

  ParseConfig(std::move(json_parser), std::move(doc), kPathForErrorStrings);
}

// static
ModularConfigReader ModularConfigReader::CreateFromNamespace() {
  return ModularConfigReader(fbl::unique_fd(open("/", O_RDONLY)));
}

ModularConfigReader::~ModularConfigReader() {}

void ModularConfigReader::ParseConfig(json::JSONParser json_parser, rapidjson::Document doc,
                                      std::string config_path) {
  std::string basemgr_json;
  std::string sessionmgr_json;
  if (json_parser.HasError()) {
    FX_LOGS(ERROR) << "Error while parsing " << config_path
                   << " to string. Error: " << json_parser.error_str();
    // Leave |basemgr_config_| and |sessionmgr_config_| empty-initialized.
    basemgr_json = "{}";
    sessionmgr_json = "{}";
  } else {
    // Parse the `basemgr` and `sessionmgr` sections out of the config.
    basemgr_json = GetSectionAsString(doc, modular_config::kBasemgrConfigName);
    sessionmgr_json = GetSectionAsString(doc, modular_config::kSessionmgrConfigName);
  }

  if (!XdrRead(basemgr_json, &basemgr_config_, XdrBasemgrConfig)) {
    FX_LOGS(ERROR) << "Unable to parse 'basemgr' from " << config_path;
  }
  if (!XdrRead(sessionmgr_json, &sessionmgr_config_, XdrSessionmgrConfig)) {
    FX_LOGS(ERROR) << "Unable to parse 'sessionmgr' from " << config_path;
  }
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

fuchsia::modular::session::SessionmgrConfig ModularConfigReader::GetDefaultSessionmgrConfig()
    const {
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config;
  XdrRead("{}", &sessionmgr_config, XdrSessionmgrConfig);
  return sessionmgr_config;
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
