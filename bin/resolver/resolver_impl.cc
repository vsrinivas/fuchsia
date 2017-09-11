// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <unordered_map>

#include "apps/maxwell/src/resolver/resolver_impl.h"

#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "third_party/rapidjson/rapidjson/schema.h"

namespace resolver {

namespace {

constexpr char kModuleFacetName[] = "fuchsia:module";

// higher is better
// TODO(rosswang): this is a very temporary notion
using MatchScore = int;
constexpr MatchScore kDefaultMatch = 0;
constexpr MatchScore kNumericMatch = 4;  // https://xkcd.com/221/
// We don't have visibility into schemas for match fidelity, so use the string
// length of the serialized JSON schema document as a proxy with a scaling
// factor.
constexpr float kSchemaMatchFactor = .1f;

// match function signature doc:
//
// Returns true on match, false otherwise. On match, |score| is populated with
// the |MatchScore| of the match (its input value is ignored). On no match, the
// behavior of |score| is undefined and it may or may not be modified.
//
// bool (*) (const rapidjson::Value& condition, const rapidjson::Value& member,
//           MatchScore* score);

bool MatchDataPrecondition(const rapidjson::Value& condition,
                           const rapidjson::Value& member,
                           MatchScore* score);

// See docs/module_manifest.md for details about these test functions.

// Accepts the data_preconditions object of a module facet and a document
// and returns whether or not the document matches the stated preconditions.
// Preconditions map property names to property values in the document.
bool MatchProperties(const rapidjson::Value& test_args,
                     const rapidjson::Value& data,
                     MatchScore* score) {
  FXL_CHECK(test_args.IsObject());
  if (!data.IsObject()) {
    return false;
  }

  *score = 0;

  for (auto it = test_args.MemberBegin(); test_args.MemberEnd() != it; ++it) {
    MatchScore member_score;
    if (!(data.HasMember(it->name.GetString()) &&
          MatchDataPrecondition(it->value, data[it->name.GetString()],
                                &member_score))) {
      return false;
    }

    *score += member_score;
  }
  return true;
}

bool MatchAny(const rapidjson::Value& test_args,
              const rapidjson::Value& member,
              MatchScore* score) {
  FXL_CHECK(test_args.IsArray());

  for (auto it = test_args.Begin(); it != test_args.End(); ++it) {
    if (MatchDataPrecondition(*it, member, score)) {
      return true;
    }
  }
  return false;
}

bool MatchDataPrecondition(const rapidjson::Value& condition,
                           const rapidjson::Value& member,
                           MatchScore* score) {
  // TODO(rosswang): if we end up supporting more condition types, switch to
  // something more expressive

  if (condition.IsObject()) {
    return MatchProperties(condition, member, score);
  } else if (condition.IsArray()) {
    return MatchAny(condition, member, score);
  } else {
    if (condition == member) {
      *score =
          condition.IsString() ? condition.GetStringLength() : kNumericMatch;
      return true;
    } else {
      return false;
    }
  }
}

struct ModuleResolution {
  std::string url;
  MatchScore score;
};

}  // namespace

void ResolverImpl::ResolveModules(const fidl::String& contract,
                                  const fidl::String& json_data,
                                  const ResolveModulesCallback& callback) {
  FXL_CHECK(!!component_index_);

  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value contract_json;
  document.AddMember("contract",
                     rapidjson::Value(contract.get().c_str(), contract.size()),
                     document.GetAllocator());

  fidl::Map<fidl::String, fidl::String> filter;
  filter[kModuleFacetName] = modular::JsonValueToString(document);

  component_index_->FindComponentManifests(
      std::move(filter),
      [callback,
       json_data](fidl::Array<component::ComponentManifestPtr> components) {
        rapidjson::Document data;
        if (!!json_data) {
          if (data.Parse(json_data.get().c_str()).HasParseError()) {
            FXL_LOG(WARNING) << "Parse error.";
            callback(fidl::Array<ModuleInfoPtr>::New(0));
            return;
          }
        }

        std::vector<ModuleResolution> raw_results;

        for (auto it = components.begin(); components.end() != it; ++it) {
          rapidjson::Document manifest;
          if (manifest.Parse((*it)->raw.get().c_str()).HasParseError()) {
            FXL_LOG(WARNING)
                << "Parse error for manifest of " << (*it)->component->url;
            continue;
          }

          const auto& url = (*it)->component->url;
          const auto& module_facet = manifest[kModuleFacetName];
          if (module_facet.HasMember("data_schema")) {
            const auto& schema_doc = module_facet["data_schema"];
            rapidjson::SchemaDocument schema(schema_doc);
            rapidjson::SchemaValidator validator(schema);
            if (data.Accept(validator)) {
              MatchScore score =
                  (MatchScore)(modular::JsonValueToString(schema_doc).length() *
                               kSchemaMatchFactor);
              FXL_LOG(INFO) << "Resolved to " << url << " with score " << score;
              raw_results.push_back({url, score});
            }
          } else if (data.IsNull() ||
                     !module_facet.HasMember("data_preconditions")) {
            FXL_LOG(INFO) << "Resolved to " << url << " with score "
                          << kDefaultMatch << " (default)";
            raw_results.push_back({url, kDefaultMatch});
          } else {
            MatchScore score;
            if (MatchDataPrecondition(module_facet["data_preconditions"], data,
                                      &score)) {
              FXL_LOG(INFO) << "Resolved to " << url << " with score " << score;
              raw_results.push_back({url, score});
            }
          }
        }

        std::sort(raw_results.begin(), raw_results.end(),
                  [](const ModuleResolution& a, const ModuleResolution& b) {
                    // best matches first
                    return a.score > b.score;
                  });

        auto results = fidl::Array<ModuleInfoPtr>::New(0);
        for (const auto& resolution : raw_results) {
          ModuleInfoPtr module(ModuleInfo::New());
          module->component_id = resolution.url;
          results.push_back(std::move(module));
        }
        callback(std::move(results));
      });
}

}  // namespace resolver
