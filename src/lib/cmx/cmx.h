// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CMX_CMX_H_
#define SRC_LIB_CMX_CMX_H_

#include <string>

#include <rapidjson/document.h>

#include "src/lib/cmx/facet_parser/cmx_facet_parser.h"
#include "src/lib/cmx/program.h"
#include "src/lib/cmx/runtime.h"
#include "src/lib/cmx/sandbox.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace component {

class CmxMetadata {
 public:
  CmxMetadata();
  ~CmxMetadata();

  // Initializes the CmxMetadata from a JSON file. Returns false if there were
  // any errors.
  bool ParseFromFileAt(int dirfd, const std::string& file, json::JSONParser* json_parser);

  bool ParseFromString(const std::string& data, const std::string& filename,
                       json::JSONParser* json_parser);

  // Returns the Facet section value if found, else returns null value.
  const rapidjson::Value& GetFacet(const std::string& key);

  const SandboxMetadata& sandbox_meta() { return sandbox_meta_; }
  const RuntimeMetadata& runtime_meta() { return runtime_meta_; }
  const ProgramMetadata& program_meta() { return program_meta_; }

 private:
  void ParseSandboxMetadata(const rapidjson::Document& document, json::JSONParser* json_parser);
  void ParseProgramMetadata(const rapidjson::Document& document, json::JSONParser* json_parser);

  bool ParseDocument(const rapidjson::Document& document, json::JSONParser* json_parser);

  SandboxMetadata sandbox_meta_;
  RuntimeMetadata runtime_meta_;
  ProgramMetadata program_meta_;
  CmxFacetParser facet_parser_;
};

}  // namespace component

#endif  // SRC_LIB_CMX_CMX_H_
