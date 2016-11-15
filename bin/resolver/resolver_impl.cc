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

bool EqualJsonDocstore(const rapidjson::Value& json,
                       const document_store::ValuePtr& docstore) {
  switch (docstore->which()) {
    case document_store::Value::Tag::IRI:
      return (json.IsString() && docstore->get_iri() == json.GetString());
    case document_store::Value::Tag::STRING_VALUE:
      return (json.IsString() &&
              docstore->get_string_value() == json.GetString());
    case document_store::Value::Tag::INT_VALUE:
      return (json.IsInt() && docstore->get_int_value() == json.GetInt());
    case document_store::Value::Tag::FLOAT_VALUE:
      return (json.IsDouble() &&
              docstore->get_float_value() == json.GetDouble());
    case document_store::Value::Tag::BINARY:
    case document_store::Value::Tag::EMPTY:
      return false;
    default:
      FTL_LOG(FATAL) << "Unsupported data type.";
  }
  FTL_LOG(FATAL) << "This should never happen.";
  return false;
}

// Accepts the data_preconditions object of a module facet and a document
// and returns whether or not the document matches the stated preconditions.
// Precodnitions map property names to property values in the document.
bool MatchDataPreconditions(const rapidjson::Value& preconditions,
                            const document_store::DocumentPtr& data) {
  FTL_CHECK(preconditions.IsObject());

  for (auto it = preconditions.MemberBegin(); preconditions.MemberEnd() != it;
       ++it) {
    auto data_it = data->properties.find(it->name.GetString());

    if (data_it == data->properties.end() ||
        !EqualJsonDocstore(it->value, data_it.GetValue())) {
      return false;
    }
  }
  return true;
}

}  // namespace internal

void ResolverImpl::ResolveModules(const fidl::String& contract,
                                  document_store::DocumentPtr data,
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
      ftl::MakeCopyable([ callback, data = std::move(data) ](
          fidl::Array<component::ComponentManifestPtr> components) {
        fidl::Array<ModuleInfoPtr> results(fidl::Array<ModuleInfoPtr>::New(0));

        for (auto it = components.begin(); components.end() != it; ++it) {
          rapidjson::Document manifest;
          if (manifest.Parse((*it)->raw.get().c_str()).HasParseError()) {
            FTL_LOG(WARNING) << "Parse error for manifest of "
                             << (*it)->component->url;
            continue;
          }

          if (!manifest[internal::kModuleFacetName].HasMember(
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
      }));
}

}  // namespace resolver
