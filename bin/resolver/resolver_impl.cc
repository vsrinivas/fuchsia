// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/maxwell/src/resolver/resolver_impl.h"

#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace resolver {

namespace internal {

constexpr char kModuleFacetName[] = "fuchsia:module";

// Accepts the data_preconditions object of a module facet and a document
// and returns whether or not the document matches the stated preconditions.
// Precodnitions map property names to property values in the document.
bool MatchDataPreconditions(const rapidjson::Value& preconditions,
                            const rapidjson::Value& data) {
  FTL_CHECK(preconditions.IsObject());
  FTL_CHECK(data.IsObject());

  for (auto it = preconditions.MemberBegin(); preconditions.MemberEnd() != it;
       ++it) {
    if (!data.HasMember(it->name.GetString()) ||
        it->value != data[it->name.GetString()]) {
      return false;
    }
  }
  return true;
}

}  // namespace internal

void ResolverImpl::ResolveModules(const fidl::String& contract,
                                  const fidl::String& json_data,
                                  const ResolveModulesCallback& callback) {
  FTL_CHECK(!!component_index_);

  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value contract_json;
  document.AddMember("contract",
                     rapidjson::Value(contract.get().c_str(), contract.size()),
                     document.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  fidl::Map<fidl::String, fidl::String> filter;
  filter[internal::kModuleFacetName] = buffer.GetString();

  component_index_->FindComponentManifests(
      std::move(filter),
      [callback, json_data](
        fidl::Array<component::ComponentManifestPtr> components) {
        rapidjson::Document data;
        if (!!json_data) {
          if (data.Parse(json_data.get().c_str()).HasParseError()) {
            FTL_LOG(WARNING) << "Parse error.";
            callback(nullptr);
            return;
          }
        }

        fidl::Array<ModuleInfoPtr> results(fidl::Array<ModuleInfoPtr>::New(0));

        for (auto it = components.begin(); components.end() != it; ++it) {
          rapidjson::Document manifest;
          if (manifest.Parse((*it)->raw.get().c_str()).HasParseError()) {
            FTL_LOG(WARNING)
                << "Parse error for manifest of " << (*it)->component->url;
            continue;
          }

          if (data.IsNull() || !manifest[internal::kModuleFacetName].HasMember(
                  "data_preconditions") ||
              internal::MatchDataPreconditions(
                  manifest[internal::kModuleFacetName]["data_preconditions"],
                  data)) {
            ModuleInfoPtr module(ModuleInfo::New());
            module->component_id = (*it)->component->url;
            results.push_back(std::move(module));
          }
        }

        callback(std::move(results));
      });
}

}  // namespace resolver
