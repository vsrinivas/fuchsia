// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_
#define SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_

#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

class JsonVisitor : public Visitor {
 public:
  JsonVisitor(rapidjson::Value* result, rapidjson::Document::AllocatorType* allocator)
      : result_(result), allocator_(allocator) {}

 private:
  void VisitValue(const Value* node, const Type* for_type) override {
    std::stringstream ss;
    PrettyPrinter printer(ss, WithoutColors, "", 0, /*header_on_every_line=*/false);
    node->PrettyPrint(for_type, printer);
    result_->SetString(ss.str(), *allocator_);
  }

  void VisitInvalidValue(const InvalidValue* node, const Type* for_type) override {
    result_->SetString("(invalid)", *allocator_);
  }

  void VisitNullValue(const NullValue* node, const Type* for_type) override { result_->SetNull(); }

  void VisitStringValue(const StringValue* node, const Type* for_type) override {
    result_->SetString(node->string().data(), node->string().size(), *allocator_);
  }

  void VisitUnionValue(const UnionValue* node, const Type* for_type) override {
    result_->SetObject();
    rapidjson::Value key;
    key.SetString(node->member().name().c_str(), *allocator_);
    result_->AddMember(key, rapidjson::Value(), *allocator_);
    JsonVisitor visitor(&(*result_)[node->member().name().c_str()], allocator_);
    node->value()->Visit(&visitor, node->member().type());
  }

  void VisitStructValue(const StructValue* node, const Type* for_type) override {
    result_->SetObject();
    for (const auto& member : node->struct_definition().members()) {
      auto it = node->fields().find(member.get());
      if (it == node->fields().end())
        continue;
      rapidjson::Value key;
      key.SetString(member->name().c_str(), *allocator_);
      result_->AddMember(key, rapidjson::Value(), *allocator_);
      JsonVisitor visitor(&(*result_)[member->name().c_str()], allocator_);
      it->second->Visit(&visitor, member->type());
    }
  }

  void VisitVectorValue(const VectorValue* node, const Type* for_type) override {
    FXL_DCHECK(for_type != nullptr);
    const Type* component_type = for_type->GetComponentType();
    FXL_DCHECK(component_type != nullptr);
    result_->SetArray();
    for (const auto& value : node->values()) {
      rapidjson::Value element;
      JsonVisitor visitor(&element, allocator_);
      value->Visit(&visitor, component_type);
      result_->PushBack(element, *allocator_);
    }
  }

  void VisitTableValue(const TableValue* node, const Type* for_type) override {
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
        it->second->Visit(&visitor, member->type());
      }
    }
  }

 private:
  rapidjson::Value* result_;
  rapidjson::Document::AllocatorType* allocator_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_JSON_VISITOR_H_
