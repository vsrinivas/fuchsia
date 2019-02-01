// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/cmx.h"

#include <trace/event.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

#include "lib/fxl/strings/substitute.h"
#include "lib/pkg_url/fuchsia_pkg_url.h"
#include "rapidjson/document.h"

namespace component {

constexpr char kSandbox[] = "sandbox";
constexpr char kProgram[] = "program";
// TODO(CF-156): Remove this logic once all URLs are fuchsia-pkg.
constexpr char kDeprecatedBarePackageUrl[] = "deprecated-bare-package-url";

CmxMetadata::CmxMetadata() = default;
CmxMetadata::~CmxMetadata() = default;

bool CmxMetadata::ParseFromFileAt(int dirfd, const std::string& file,
                                  json::JSONParser* json_parser) {
  TRACE_DURATION("cmx", "CmxMetadata::ParseFromFileAt", "file", file);
  rapidjson::Document document = json_parser->ParseFromFileAt(dirfd, file);
  if (json_parser->HasError()) {
    return false;
  }
  if (!document.IsObject()) {
    json_parser->ReportError("File is not a JSON object.");
    return false;
  }
  ParseDeprecatedBarePackageUrl(document, json_parser);
  ParseSandboxMetadata(document, json_parser);
  runtime_meta_.ParseFromDocument(document, json_parser);
  ParseProgramMetadata(document, json_parser);
  facet_parser_.Parse(document, json_parser);
  return !json_parser->HasError();
}

void CmxMetadata::ParseDeprecatedBarePackageUrl(
    const rapidjson::Document& document, json::JSONParser* json_parser) {
  auto package_url = document.FindMember(kDeprecatedBarePackageUrl);
  if (package_url == document.MemberEnd()) {
    deprecated_bare_package_url_ = false;
  } else if (!package_url->value.IsBool()) {
    deprecated_bare_package_url_ = false;
    json_parser->ReportError("'deprecated-bare-package-url' is not a bool.");
  } else {
    deprecated_bare_package_url_ = package_url->value.GetBool();
  }
}

const rapidjson::Value& CmxMetadata::GetFacet(const std::string& key) {
  return facet_parser_.GetSection(key);
}

void CmxMetadata::ParseSandboxMetadata(const rapidjson::Document& document,
                                       json::JSONParser* json_parser) {
  auto sandbox = document.FindMember(kSandbox);
  if (sandbox == document.MemberEnd()) {
    // Valid syntax, but no value. Pass empty object.
    rapidjson::Value sandbox_obj = rapidjson::Value(rapidjson::kObjectType);
    sandbox_meta_.Parse(sandbox_obj, json_parser);
  } else if (!sandbox->value.IsObject()) {
    json_parser->ReportError("'sandbox' is not an object.");
    return;
  } else {
    sandbox_meta_.Parse(sandbox->value, json_parser);
  }
}

void CmxMetadata::ParseProgramMetadata(const rapidjson::Document& document,
                                       json::JSONParser* json_parser) {
  auto program = document.FindMember(kProgram);
  if (program == document.MemberEnd()) {
    // Valid syntax, but no value.
    return;
  }
  if (!program->value.IsObject()) {
    json_parser->ReportError("'program' is not an object.");
    return;
  }
  program_meta_.Parse(program->value, json_parser);
}

}  // namespace component
