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

bool CmxMetadata::ParseSandboxMetadata(const std::string& data,
                                       rapidjson::Value* parsed_value) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;
  auto sandbox = document.FindMember(kSandbox);
  if (sandbox == document.MemberEnd() || !sandbox->value.IsObject())
    return false;

  *parsed_value = sandbox->value;
  return true;
}

bool CmxMetadata::ParseProgramMetadata(const std::string& data,
                                       rapidjson::Value* parsed_value) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;
  auto program = document.FindMember(kProgram);
  if (program == document.MemberEnd() || !program->value.IsObject())
    return false;

  *parsed_value = program->value;
  return true;
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

}  // namespace component
