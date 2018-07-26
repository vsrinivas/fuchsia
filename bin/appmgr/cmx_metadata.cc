// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/cmx_metadata.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kSandbox[] = "sandbox";
constexpr char kProgram[] = "program";
constexpr char kCmxPath[] = "meta/";
constexpr char kCmxExtension[] = ".cmx";
static const std::regex* const kPackageNameFileScheme =
    new std::regex("^file:///pkgfs/packages/(.*?)/");
static const std::regex* const kPackageName =
    new std::regex("^/pkgfs/packages/(.*?)/");
static const std::regex* const kCmxName = new std::regex("meta/(.*?)\\.cmx");

CmxMetadata::CmxMetadata() = default;

CmxMetadata::~CmxMetadata() = default;

bool CmxMetadata::ParseFromFileAt(int dirfd, const std::string& file) {
  rapidjson::Document document = json_parser_.ParseFromFileAt(dirfd, file);
  if (HasError()) {
    return false;
  }
  if (!document.IsObject()) {
    json_parser_.ReportError("File is not a JSON object.");
    return false;
  }
  ParseSandboxMetadata(document);
  runtime_meta_.ParseFromDocument(document, &json_parser_);
  ParseProgramMetadata(document);
  return !HasError();
}

bool CmxMetadata::HasError() const {
  return json_parser_.HasError();
}

std::string CmxMetadata::error_str() const {
  return json_parser_.error_str();
}

std::string CmxMetadata::GetCmxPathFromFullPackagePath(
    const std::string& package_resolved_url) {
  // Expect package resolved URL in the form of file:///pkgfs/packages/<FOO>/0.
  // Look for <FOO> as the package name.
  // Currently there is only one component per package. The default .cmx is
  // meta/<FOO>.cmx
  return GetCmxPathFromPath(*kPackageNameFileScheme, package_resolved_url);
}

std::string CmxMetadata::ExtractRelativeCmxPath(
    const std::string& cmx_resolved_url) {
  // Expect package resolved URL in the form of
  // file:///pkgfs/packages/<FOO>/0/meta/<BAR>.cmx. Look for <BAR> as the
  // manifest name.
  return GetCmxPathFromPath(*kCmxName, cmx_resolved_url);
}

std::string CmxMetadata::GetCmxPathFromPath(const std::regex& regex,
                                            const std::string& url) {
  std::string cmx_path;
  std::smatch sm;
  if (std::regex_search(url, sm, regex) && sm.size() >= 2) {
    std::ostringstream os;

    os << kCmxPath << sm[1].str().c_str() << kCmxExtension;
    cmx_path = os.str();
  }
  return cmx_path;
}

bool CmxMetadata::IsCmxExtension(const std::string& path) {
  return (path.size() >= strlen(kCmxExtension) &&
          path.compare(path.size() - strlen(kCmxExtension),
                       strlen(kCmxExtension), kCmxExtension) == 0);
}

std::string CmxMetadata::GetPackageNameFromCmxPath(
    const std::string& cmx_path) {
  if (!IsCmxExtension(cmx_path)) {
    return "";
  }

  std::smatch sm;
  // Expect cmx path in the form of /pkgfs/packages/<FOO>/0/meta/<BAR>.cmx
  // Look for <FOO> as the package name.
  if (std::regex_search(cmx_path, sm, *kPackageName) && sm.size() >= 2) {
    return sm[1].str();
  }
  return "";
}

void CmxMetadata::ParseSandboxMetadata(const rapidjson::Document& document) {
  auto sandbox = document.FindMember(kSandbox);
  if (sandbox == document.MemberEnd()) {
    // Valid syntax, but no value.
    return;
  }
  if (!sandbox->value.IsObject()) {
    json_parser_.ReportError("'sandbox' is not an object.");
    return;
  }

  sandbox_meta_.Parse(sandbox->value, &json_parser_);
}

void CmxMetadata::ParseProgramMetadata(const rapidjson::Document& document) {
  auto program = document.FindMember(kProgram);
  if (program == document.MemberEnd()) {
    // Valid syntax, but no value.
    return;
  }
  if (!program->value.IsObject()) {
    json_parser_.ReportError("'program' is not an object.");
    return;
  }
  program_meta_.Parse(program->value, &json_parser_);
}

}  // namespace component
