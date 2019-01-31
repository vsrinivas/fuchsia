// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest/module_manifest_xdr.h"

#include "gtest/gtest.h"
#include "peridot/lib/fidl/json_xdr.h"

void ExpectBasicManifest(const char manifest_str[]) {
  fuchsia::modular::ModuleManifest m;
  EXPECT_TRUE(XdrRead(manifest_str, &m, modular::XdrModuleManifest));
  EXPECT_EQ("binary", m.binary);
  EXPECT_EQ("suggestion_headline", m.suggestion_headline);

  EXPECT_EQ(1u, m.intent_filters->size());
  EXPECT_EQ(1u, m.intent_filters->at(0).parameter_constraints.size());
  EXPECT_EQ("name", m.intent_filters->at(0).parameter_constraints.at(0).name);
  EXPECT_EQ("type", m.intent_filters->at(0).parameter_constraints.at(0).type);
}

// Tests version 4 of the manifest
TEST(XdrModuleManifestTest, BasicVersion2) {
  ExpectBasicManifest(R"(
    {
      "@version": 2,
      "binary": "binary",
      "suggestion_headline": "suggestion_headline",
      "intent_filters": [
        {
          "action": "action",
          "parameters": [{
            "name": "name",
            "type": "type"
          }]
        }
      ]
    }
  )");
}

TEST(XdrModuleManifestTest, BasicVersion1) {
  ExpectBasicManifest(R"(
    {
      "binary": "binary",
      "suggestion_headline": "suggestion_headline",
      "action": "action",
      "parameters": [{
        "name": "name",
        "type": "type"
      }]
    }
  )");
}
