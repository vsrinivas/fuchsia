// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_
#define SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_

#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {

class JsonVisitor : public Visitor {
 public:
  explicit JsonVisitor(rapidjson::Value* result, rapidjson::Document::AllocatorType* allocator)
      : result_(result), allocator_(allocator) {}

 private:
  void VisitValue(const Value* node) override {
    std::stringstream ss;
    node->PrettyPrint(ss, WithoutColors, nullptr, "", 0, 0, 0);
    result_->SetString(ss.str(), *allocator_);
  }

  void VisitStringValue(const StringValue* node) override {
    if (node->is_null()) {
      result_->SetNull();
    } else if (auto str = node->string()) {
      result_->SetString(str->data(), str->size(), *allocator_);
    } else {
      result_->SetString("(invalid)", *allocator_);
    }
  }

  void VisitObject(const Object* node) override {
    if (node->is_null()) {
      result_->SetNull();
    } else {
      result_->SetObject();
      for (const auto& member : node->struct_definition().members()) {
        auto it = node->fields().find(std::string(member->name()));
        if (it == node->fields().end())
          continue;
        const auto& [name, value] = *it;
        rapidjson::Value key;
        key.SetString(name.c_str(), *allocator_);
        result_->AddMember(key, rapidjson::Value(), *allocator_);
        JsonVisitor visitor(&(*result_)[name.c_str()], allocator_);
        value->Visit(&visitor);
      }
    }
  }

  void VisitEnvelopeValue(const EnvelopeValue* node) override {
    if (node->is_null() || (node->value() == nullptr)) {
      result_->SetNull();
    } else {
      node->value()->Visit(this);
    }
  }

  void VisitTableValue(const TableValue* node) override {
    result_->SetObject();
    for (const auto& field : node->envelopes()) {
      if ((field.value() != nullptr) && !field.value()->is_null()) {
        rapidjson::Value key;
        key.SetString(field.name().c_str(), *allocator_);
        result_->AddMember(key, rapidjson::Value(), *allocator_);
        JsonVisitor visitor(&(*result_)[field.name().c_str()], allocator_);
        field.value()->Visit(&visitor);
      }
    }
  }

  void VisitUnionValue(const UnionValue* node) override {
    if (node->is_null() || (node->field().value()->is_null())) {
      result_->SetNull();
    } else {
      result_->SetObject();
      rapidjson::Value key;
      key.SetString(node->field().name().c_str(), *allocator_);
      result_->AddMember(key, rapidjson::Value(), *allocator_);
      JsonVisitor visitor(&(*result_)[node->field().name().c_str()], allocator_);
      if (node->field().value() != nullptr) {
        node->field().value()->Visit(&visitor);
      }
    }
  }

  void VisitArrayValue(const ArrayValue* node) override {
    result_->SetArray();
    for (const auto& value : node->values()) {
      rapidjson::Value element;
      JsonVisitor visitor(&element, allocator_);
      value->Visit(&visitor);
      result_->PushBack(element, *allocator_);
    }
  }

  void VisitVectorValue(const VectorValue* node) override {
    if (node->is_null()) {
      result_->SetNull();
    } else {
      result_->SetArray();
      for (const auto& value : node->values()) {
        rapidjson::Value element;
        JsonVisitor visitor(&element, allocator_);
        value->Visit(&visitor);
        result_->PushBack(element, *allocator_);
      }
    }
  }

  void VisitEnumValue(const EnumValue* node) override {
    if (auto data = node->data()) {
      std::string name = node->enum_definition().GetNameFromBytes(data->data());
      result_->SetString(name.c_str(), *allocator_);
    } else {
      result_->SetString("(invalid)", *allocator_);
    }
  }

  void VisitBitsValue(const BitsValue* node) override {
    if (auto data = node->data()) {
      std::string name = node->bits_definition().GetNameFromBytes(data->data());
      result_->SetString(name.c_str(), *allocator_);
    } else {
      result_->SetString("(invalid)", *allocator_);
    }
  }

 private:
  rapidjson::Value* result_;
  rapidjson::Document::AllocatorType* allocator_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_
