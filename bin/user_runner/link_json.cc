// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/link_json.h"

#include "lib/ftl/logging.h"

namespace modular {

namespace {

int next_id{};

// Returns ID of the document created for the object.
fidl::String ConvertObject(LinkData* const ret, const rapidjson::Value::ConstObject& src) {
  auto doc = document_store::Document::New();
  auto docid = doc->docid = std::string("doc") + std::to_string(next_id++);

  for (auto& property : src) {
    switch (property.value.GetType()) {
      case rapidjson::kNullType: {
        auto val = document_store::Value::New();
        val->set_empty(true);
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kFalseType: {
        auto val = document_store::Value::New();
        val->set_int_value(0);
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kTrueType: {
        auto val = document_store::Value::New();
        val->set_int_value(1);
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kObjectType: {
        auto val = document_store::Value::New();
        val->set_iri(ConvertObject(ret, property.value.GetObject()));
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kArrayType:
        FTL_DCHECK(false) << "Cannot store a JSON Array in a Link value.";
        break;

      case rapidjson::kStringType: {
        auto val = document_store::Value::New();
        val->set_string_value(property.value.GetString());
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kNumberType: {
        auto val = document_store::Value::New();
        if (property.value.IsInt()) {
          val->set_int_value(property.value.GetInt());
        } else {
          val->set_float_value(property.value.GetDouble());
        }
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }
    }
  }

  ret->insert(docid, std::move(doc));
  return docid;
}

}  // namespace

LinkData ConvertToLink(const rapidjson::Document& src) {
  LinkData ret;

  FTL_DCHECK(src.IsObject()) << "Link value must be an Object.";

  if (src.IsObject()) {
    ConvertObject(&ret, src.GetObject());
  }

  return ret;
}

}  // namespace modular
