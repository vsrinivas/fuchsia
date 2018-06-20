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
namespace {}  // namespace

constexpr char kSandbox[] = "sandbox";
constexpr char kCmxPath[] = "meta/";
constexpr char kCmxExtension[] = ".cmx";
static const std::regex package_name("/packages/(.*?)/");

CmxMetadata::CmxMetadata() = default;

CmxMetadata::~CmxMetadata() = default;

bool CmxMetadata::ParseSandboxMetadata(const std::string& data,
                                       rapidjson::Value* parsed_value) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;
  auto sandbox = document.FindMember(kSandbox);

  *parsed_value = sandbox->value;
  return true;
}

std::string CmxMetadata::GetCmxPath(std::string package_resolved_url) {
  std::string cmx_path;
  std::smatch sm;
  // Expect package resolved URL in the form of file:///pkgfs/packages/<FOO>/0
  // Look for <FOO> as the package name.
  if (std::regex_search(package_resolved_url, sm, package_name) &&
      sm.size() >= 2) {
    std::ostringstream os;
    // Currently there is only one component per package. The default .cmx is
    // meta/<FOO>.cmx
    os << kCmxPath << sm[1].str().c_str() << kCmxExtension;
    cmx_path = os.str();
  }
  return cmx_path;
}

}  // namespace component
