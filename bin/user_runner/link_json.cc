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
  constexpr char id[] = "@id";

  // If a JSON object defines the @id property, then it's the @id of
  // the corresponding Document.
  //
  // Notice that there is no difference in expressing a document and a
  // reference to it. It just works as two JSON Objects with the
  // same @id:
  //
  // {
  //   "foo": {
  //     "@id": "http://foo.com/1",
  //     "content": "foo content"
  //   },
  //   "foo-ref": {
  //     "@id": "http://foo.com/1"
  //   }
  // }
  //
  // Here, the value of both foo and foo-ref is the same Document,
  // referenced by the the same ID.
  std::string docid;
  if (src.HasMember(id)) {
    if (src[id].IsString()) {
      docid = src[id].GetString();
    } else {
      FTL_LOG(ERROR) << id << " property value must be a string. Ignoring.";
    }
  }

  if (docid.empty()) {
    docid = std::string("doc") + std::to_string(next_id++);
  }

  if (ret->find(docid) == ret->end()) {
    ret->insert(docid, document_store::Document::New());
    ret->at(docid)->docid = docid;
  }

  document_store::Document* const doc = ret->at(docid).get();

  for (auto& property : src) {
    if (property.name == id) {
      continue;
    }

    switch (property.value.GetType()) {
      case rapidjson::kNullType: {
        auto val = document_store::Value::New();
        val->set_empty(true);
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kFalseType: {
        auto val = document_store::Value::New();
        val->set_bool_value(false);
        doc->properties[property.name.GetString()] = std::move(val);
        break;
      }

      case rapidjson::kTrueType: {
        auto val = document_store::Value::New();
        val->set_bool_value(true);
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
        FTL_LOG(ERROR) << "Cannot store a JSON Array in a Link value. "
                       << "Ignoring property: " << property.name.GetString();
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
