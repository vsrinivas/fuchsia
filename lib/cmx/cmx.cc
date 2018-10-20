// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/cmx.h"

#include <trace/event.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

#include "rapidjson/document.h"

namespace component {

constexpr char kSandbox[] = "sandbox";
constexpr char kProgram[] = "program";
constexpr char kFacets[] = "facets";
constexpr char kCmxPath[] = "meta/";
constexpr char kCmxExtension[] = ".cmx";

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
  ParseSandboxMetadata(document, json_parser);
  runtime_meta_.ParseFromDocument(document, json_parser);
  ParseProgramMetadata(document, json_parser);
  ParseFacetsMetadata(document, json_parser);
  return !json_parser->HasError();
}

bool CmxMetadata::ParseFromDeprecatedRuntimeFileAt(
    int dirfd, const std::string& file, json::JSONParser* json_parser) {
  rapidjson::Document document = json_parser->ParseFromFileAt(dirfd, file);
  if (json_parser->HasError()) {
    return false;
  }
  if (!document.IsObject()) {
    json_parser->ReportError("File is not a JSON object.");
    return false;
  }
  runtime_meta_.ParseFromDocument(document, json_parser);
  return !json_parser->HasError();
}

std::string CmxMetadata::GetDefaultComponentCmxPath(
    const std::string& package_resolved_url) {
  TRACE_DURATION("cmx", "CmxMetadata::GetDefaultComponentCmxPath",
                 "package_resolved_url", package_resolved_url);
  // Expect package resolved URL in the form of file:///pkgfs/packages/<FOO>/0.
  // Look for <FOO> as the package name.
  // Currently there is only one component per package. The default .cmx is
  // meta/<FOO>.cmx.
  std::string cmx_path;
  std::ostringstream os;
  std::string component_name = GetDefaultComponentName(package_resolved_url);
  if (!component_name.empty()) {
    os << kCmxPath << component_name << kCmxExtension;
    cmx_path = os.str();
  }
  return cmx_path;
}

std::string CmxMetadata::GetDefaultComponentName(
    const std::string& package_resolved_url) {
  static const std::regex* const kPackageNameFileScheme =
      new std::regex("^file:///pkgfs/packages/(.*?)/");
  // Expect package resolved URL in the form of file:///pkgfs/packages/<FOO>/0.
  // Look for <FOO> as the package name.
  // Currently there is only one component per package. The default component is
  // <FOO>.
  std::string component_name;
  std::smatch sm;
  if (std::regex_search(package_resolved_url, sm, *kPackageNameFileScheme) &&
      sm.size() >= 2) {
    component_name = sm[1].str().c_str();
  }
  return component_name;
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

void CmxMetadata::ParseFacetsMetadata(const rapidjson::Document& document,
                                      json::JSONParser* json_parser) {
  auto facets = document.FindMember(kFacets);
  if (facets == document.MemberEnd()) {
    // Valid syntax, but no value.
    return;
  }
  facets_meta_.Parse(facets->value, json_parser);
}

}  // namespace component
