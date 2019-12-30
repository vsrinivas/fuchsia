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

  void VisitInvalidValue(const InvalidValue* node) override {
    result_->SetString("(invalid)", *allocator_);
  }

  void VisitNullValue(const NullValue* node) override { result_->SetNull(); }

  void VisitStringValue(const StringValue* node) override {
    result_->SetString(node->string().data(), node->string().size(), *allocator_);
  }

  void VisitStructValue(const StructValue* node) override {
    result_->SetObject();
    for (const auto& member : node->struct_definition().members()) {
      auto it = node->fields().find(member.get());
      if (it == node->fields().end())
        continue;
      rapidjson::Value key;
      key.SetString(member->name().c_str(), *allocator_);
      result_->AddMember(key, rapidjson::Value(), *allocator_);
      JsonVisitor visitor(&(*result_)[member->name().c_str()], allocator_);
      it->second->Visit(&visitor);
    }
  }

  void VisitTableValue(const TableValue* node) override {
    result_->SetObject();
    for (const auto& member : node->table_definition().members()) {
      if ((member != nullptr) && !member->reserved()) {
        auto it = node->members().find(member.get());
        if ((it == node->members().end()) || it->second->IsNull())
          continue;
        rapidjson::Value key;
        key.SetString(member->name().c_str(), *allocator_);
        result_->AddMember(key, rapidjson::Value(), *allocator_);
        JsonVisitor visitor(&(*result_)[member->name().c_str()], allocator_);
        it->second->Visit(&visitor);
      }
    }
  }

  void VisitUnionValue(const UnionValue* node) override {
    result_->SetObject();
    rapidjson::Value key;
    key.SetString(node->member().name().c_str(), *allocator_);
    result_->AddMember(key, rapidjson::Value(), *allocator_);
    JsonVisitor visitor(&(*result_)[node->member().name().c_str()], allocator_);
    node->value()->Visit(&visitor);
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
    if (node->IsNull()) {
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
