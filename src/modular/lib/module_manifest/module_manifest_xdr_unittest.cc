// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/module_manifest/module_manifest_xdr.h"

#include "gtest/gtest.h"
#include "src/modular/lib/fidl/json_xdr.h"

void ExpectBasicManifest(const char manifest_str[]) {
  fuchsia::modular::ModuleManifest m;
  EXPECT_TRUE(XdrRead(manifest_str, &m, modular::XdrModuleManifest));
  EXPECT_EQ("suggestion_headline", m.suggestion_headline);

  EXPECT_EQ(1u, m.intent_filters->size());
  EXPECT_EQ("action", m.intent_filters->at(0).action);
  EXPECT_EQ(1u, m.intent_filters->at(0).parameter_constraints.size());
  EXPECT_EQ("name", m.intent_filters->at(0).parameter_constraints.at(0).name);
  EXPECT_EQ("type", m.intent_filters->at(0).parameter_constraints.at(0).type);
}

// Tests version 2 of the manifest with "binary" specified
TEST(XdrModuleManifestTest, BasicVersion2WithBinary) {
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

// Tests version 2 of the manifest
TEST(XdrModuleManifestTest, BasicVersion2) {
  ExpectBasicManifest(R"(
    {
      "@version": 2,
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

void ExpectManifestWithCompositionPatternNoParameters(const char manifest_str[],
                                                      bool expect_success = true) {
  fuchsia::modular::ModuleManifest m;
  bool success = XdrRead(manifest_str, &m, modular::XdrModuleManifest);
  EXPECT_EQ(success, expect_success);
  if (!expect_success)
    return;

  EXPECT_EQ("ticker", m.composition_pattern);
  EXPECT_EQ("suggestion_headline", m.suggestion_headline);

  EXPECT_EQ(1u, m.intent_filters->size());
  EXPECT_EQ("action", m.intent_filters->at(0).action);
  EXPECT_EQ(0u, m.intent_filters->at(0).parameter_constraints.size());
}

void FailManifestWithCompositionPatternNoParameters(const char manifest_str[]) {
  ExpectManifestWithCompositionPatternNoParameters(manifest_str, false);
}

TEST(XdrModuleManifestTest, ReorderedWithCompositionPatternAndNoParameters) {
  ExpectManifestWithCompositionPatternNoParameters(R"(
    {
      "@version": 2,
      "composition_pattern": "ticker",
      "intent_filters": [
        {
          "action": "action",
          "parameters": []
        }
      ],
      "suggestion_headline": "suggestion_headline"
    }
  )");
}

TEST(XdrModuleManifestTest, MissingParameters) {
  FailManifestWithCompositionPatternNoParameters(R"(
    {
      "@version": 2,
      "composition_pattern": "ticker",
      "intent_filters": [
        {
          "action": "action"
        }
      ],
      "suggestion_headline": "suggestion_headline"
    }
  )");
}
