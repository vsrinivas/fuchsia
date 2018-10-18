// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CMX_CMX_H_
#define GARNET_LIB_CMX_CMX_H_

#include <regex>
#include <string>

#include "garnet/lib/cmx/facets.h"
#include "garnet/lib/cmx/program.h"
#include "garnet/lib/cmx/runtime.h"
#include "garnet/lib/cmx/sandbox.h"
#include "garnet/lib/json/json_parser.h"
#include "garnet/lib/pkg_url/fuchsia_pkg_url.h"
#include "rapidjson/document.h"

namespace component {

class CmxMetadata {
 public:
  CmxMetadata();
  ~CmxMetadata();

  // Initializes the CmxMetadata from a JSON file. Returns false if there were
  // any errors.
  bool ParseFromFileAt(int dirfd, const std::string& file,
                       json::JSONParser* json_parser);

  // Initializes the CmxMetadata from a deprecated_runtime JSON file. Returns
  // false if there were any errors.
  bool ParseFromDeprecatedRuntimeFileAt(int dirfd, const std::string& file,
                                        json::JSONParser* json_parser);

  // Takes a package's parsed resolved_url and returns the default component's
  // .cmx path, i.e. meta/<package_name>.cmx.
  static std::string GetDefaultComponentCmxPath(
      const FuchsiaPkgUrl& package_resolved_url);

  // Takes a package's parsed resolved_url and returns the default component's
  // .cmx path, i.e. <package_name>.cmx.
  static std::string GetDefaultComponentName(
      const FuchsiaPkgUrl& package_resolved_url);

  const SandboxMetadata& sandbox_meta() { return sandbox_meta_; }
  const RuntimeMetadata& runtime_meta() { return runtime_meta_; }
  const ProgramMetadata& program_meta() { return program_meta_; }
  const FacetsMetadata& facets_meta() { return facets_meta_; }

 private:
  static std::string GetCmxPathFromPath(const std::regex& regex,
                                        const std::string& path);
  void ParseSandboxMetadata(const rapidjson::Document& document,
                            json::JSONParser* json_parser);
  void ParseProgramMetadata(const rapidjson::Document& document,
                            json::JSONParser* json_parser);
  void ParseFacetsMetadata(const rapidjson::Document& document,
                           json::JSONParser* json_parser);

  SandboxMetadata sandbox_meta_;
  RuntimeMetadata runtime_meta_;
  ProgramMetadata program_meta_;
  FacetsMetadata facets_meta_;
};

}  // namespace component

#endif  // GARNET_LIB_CMX_CMX_H_
