// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_CMX_METADATA_H_
#define GARNET_BIN_APPMGR_CMX_METADATA_H_

#include <regex>
#include <string>

#include "garnet/bin/appmgr/program_metadata.h"
#include "garnet/bin/appmgr/runtime_metadata.h"
#include "garnet/bin/appmgr/sandbox_metadata.h"
#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

class CmxMetadata {
 public:
  CmxMetadata();
  ~CmxMetadata();

  // Initializes the CmxMetadata from a JSON file. Returns false if there were
  // any errors.
  bool ParseFromFileAt(int dirfd, const std::string& file);

  // Returns true if |ParseSandboxMetadata| encountered an error.
  bool HasError() const;
  // Returns the error if |HasError| is true.
  std::string error_str() const;

  // Takes a package's resolved_url, e.g. file:///pkgfs/packages/<FOO>/0, and
  // returns the default component's .cmx path, e.g. meta/<FOO>.cmx.
  static std::string GetCmxPathFromFullPackagePath(
      const std::string& package_resolved_url);

  // Takes a manifest's resolved_url, e.g.
  // file:///pkgfs/packages/<FOO>/0/meta/<BAR>.cmx, and returns the package
  // relative .cmx path, e.g. meta/<BAR>.cmx.
  static std::string ExtractRelativeCmxPath(
      const std::string& cmx_resolved_url);

  // Returns true if path ends in .cmx, false otherwise.
  static bool IsCmxExtension(const std::string& path);

  // Returns the package name from a .cmx file's full /pkgfs path. Returns the
  // empty string "" if unmatched.
  static std::string GetPackageNameFromCmxPath(const std::string& cmx_path);

  const SandboxMetadata& sandbox_meta() { return sandbox_meta_; }
  const RuntimeMetadata& runtime_meta() { return runtime_meta_; }
  const ProgramMetadata& program_meta() { return program_meta_; }

 private:
  static std::string GetCmxPathFromPath(const std::regex& regex,
                                        const std::string& path);
  void ParseSandboxMetadata(const rapidjson::Document& document);
  void ParseProgramMetadata(const rapidjson::Document& document);

  json::JSONParser json_parser_;
  SandboxMetadata sandbox_meta_;
  RuntimeMetadata runtime_meta_;
  ProgramMetadata program_meta_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_CMX_METADATA_H_
