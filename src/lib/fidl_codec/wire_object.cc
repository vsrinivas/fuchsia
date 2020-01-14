// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_object.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/json_visitor.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

void InvalidValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitInvalidValue(this, for_type);
}

void NullValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitNullValue(this, for_type);
}

int RawValue::DisplaySize(const Type* /*for_type*/, int /*remaining_size*/) const {
  return (data_.size() == 0) ? 0 : static_cast<int>(data_.size()) * 3 - 1;
}

void RawValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  if (data_.size() == 0) {
    return;
  }
  size_t buffer_size = data_.size() * 3;
  std::vector<char> buffer(buffer_size);
  for (size_t i = 0; i < data_.size(); ++i) {
    if (i != 0) {
      buffer[i * 3 - 1] = ' ';
    }
    snprintf(buffer.data() + (i * 3), 4, "%02x", data_[i]);
  }
  printer << buffer.data();
}

void RawValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitRawValue(this, for_type);
}

int BoolValue::DisplaySize(const Type* /*for_type*/, int /*remaining_size*/) const {
  constexpr int kTrueSize = 4;
  constexpr int kFalseSize = 5;
  return value_ ? kTrueSize : kFalseSize;
}

void BoolValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  printer << Blue << (value_ ? "true" : "false") << ResetColor;
}

void BoolValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitBoolValue(this, for_type);
}

uint8_t IntegerValue::GetUint8Value() const {
  return (!negative_ && (absolute_value_ < 0x100)) ? static_cast<uint8_t>(absolute_value_) : 0;
}

int IntegerValue::DisplaySize(const Type* for_type, int /*remaining_size*/) const {
  bool sign_size = negative_ ? 1 : 0;
  return std::to_string(absolute_value_).size() + sign_size;
}

void IntegerValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  FXL_DCHECK(for_type != nullptr);
  for_type->PrettyPrint(this, printer);
}

void IntegerValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitIntegerValue(this, for_type);
}

int DoubleValue::DisplaySize(const Type* for_type, int /*remaining_size*/) const {
  return std::to_string(value_).size();
}

void DoubleValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  FXL_DCHECK(for_type != nullptr);
  for_type->PrettyPrint(this, printer);
}

void DoubleValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitDoubleValue(this, for_type);
}

int StringValue::DisplaySize(const Type* /*for_type*/, int /*remaining_size*/) const {
  return static_cast<int>(string_.size()) + 2;  // The two quotes.
}

void StringValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  printer << Red << '"' << string_ << '"' << ResetColor;
}

void StringValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitStringValue(this, for_type);
}

int HandleValue::DisplaySize(const Type* /*for_type*/, int /*remaining_size*/) const {
  return std::to_string(handle_.handle).size();
}

void HandleValue::PrettyPrint(const Type* /*for_type*/, PrettyPrinter& printer) const {
  printer.DisplayHandle(handle_);
}

void HandleValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitHandleValue(this, for_type);
}

int UnionValue::DisplaySize(const Type* for_type, int remaining_size) const {
  // Two characters for the opening brace ("{ ") + three characters for equal
  // (" = ") and two characters for the closing brace (" }").
  constexpr int kExtraSize = 7;
  int size = static_cast<int>(member_.name().size()) + kExtraSize;
  // Two characters for ": ".
  size += static_cast<int>(member_.type()->Name().size()) + 2;
  size += value_->DisplaySize(member_.type(), remaining_size - size);
  return size;
}

void UnionValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  if (DisplaySize(for_type, printer.remaining_size()) <= printer.remaining_size()) {
    std::string type_name = member_.type()->Name();
    printer << "{ " << member_.name() << ": " << Green << type_name << ResetColor << " = ";
    value_->PrettyPrint(member_.type(), printer);
    printer << " }";
  } else {
    std::string type_name = member_.type()->Name();
    printer << "{\n";
    {
      Indent indent(printer);
      printer << member_.name() << ": " << Green << type_name << ResetColor << " = ";
      value_->PrettyPrint(member_.type(), printer);
      printer << '\n';
    }
    printer << "}";
  }
}

void UnionValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitUnionValue(this, for_type);
}

int StructValue::DisplaySize(const Type* for_type, int remaining_size) const {
  int size = 0;
  for (const auto& member : struct_definition_.members()) {
    auto it = fields_.find(member.get());
    if (it == fields_.end())
      continue;
    // Two characters for the separator ("{ " or ", ") and three characters for
    // equal (" = ").
    constexpr int kExtraSize = 5;
    size += static_cast<int>(member->name().size()) + kExtraSize;
    // Two characters for ": ".
    size += static_cast<int>(member->type()->Name().size()) + 2;
    size += it->second->DisplaySize(member->type(), remaining_size - size);
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void StructValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  if (fields_.empty()) {
    printer << "{}";
  } else if (DisplaySize(for_type, printer.remaining_size()) <= printer.remaining_size()) {
    const char* separator = "{ ";
    for (const auto& member : struct_definition_.members()) {
      auto it = fields_.find(member.get());
      if (it == fields_.end())
        continue;
      printer << separator << member->name() << ": " << Green << member->type()->Name()
              << ResetColor << " = ";
      it->second->PrettyPrint(member->type(), printer);
      separator = ", ";
    }
    printer << " }";
  } else {
    printer << "{\n";
    {
      Indent indent(printer);
      for (const auto& member : struct_definition_.members()) {
        auto it = fields_.find(member.get());
        if (it == fields_.end())
          continue;
        std::string type_name = member->type()->Name();
        printer << member->name() << ": " << Green << type_name << ResetColor << " = ";
        it->second->PrettyPrint(member->type(), printer);
        printer << "\n";
      }
    }
    printer << '}';
  }
}

void StructValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitStructValue(this, for_type);
}

void StructValue::ExtractJson(rapidjson::Document::AllocatorType& allocator,
                              rapidjson::Value& result) const {
  JsonVisitor visitor(&result, &allocator);

  Visit(&visitor, nullptr);
}

int VectorValue::DisplaySize(const Type* for_type, int remaining_size) const {
  FXL_DCHECK(for_type != nullptr);
  if (values_.empty()) {
    return 2;  // The two brackets.
  }
  if (is_string_) {
    return static_cast<int>(values_.size() + 2);  // The string and the two quotes.
  }
  const Type* component_type = for_type->GetComponentType();
  FXL_DCHECK(component_type != nullptr);
  int size = 0;
  for (const auto& value : values_) {
    // Two characters for the separator ("[ " or ", ").
    size += value->DisplaySize(component_type, remaining_size - size) + 2;
    if (size > remaining_size) {
      return size;
    }
  }
  // Two characters for the closing bracket (" ]").
  size += 2;
  return size;
}

void VectorValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  FXL_DCHECK(for_type != nullptr);
  if (values_.empty()) {
    printer << "[]";
  } else if (is_string_) {
    if (has_new_line_) {
      printer << "[\n";
      {
        Indent indent(printer);
        for (const auto& value : values_) {
          printer << value->GetUint8Value();
        }
      }
      printer << '\n';
      printer << ']';
    } else {
      printer << '"';
      for (const auto& value : values_) {
        printer << value->GetUint8Value();
      }
      printer << '"';
    }
  } else if (DisplaySize(for_type, printer.remaining_size()) <= printer.remaining_size()) {
    const Type* component_type = for_type->GetComponentType();
    FXL_DCHECK(component_type != nullptr);
    const char* separator = "[ ";
    for (const auto& value : values_) {
      printer << separator;
      separator = ", ";
      value->PrettyPrint(component_type, printer);
    }
    printer << " ]";
  } else {
    const Type* component_type = for_type->GetComponentType();
    FXL_DCHECK(component_type != nullptr);
    printer << "[\n";
    {
      Indent indent(printer);
      for (const auto& value : values_) {
        int value_size = value->DisplaySize(component_type, printer.remaining_size());
        if (!printer.LineEmpty()) {
          if (value_size + 3 > printer.remaining_size()) {
            printer << "\n";
          } else {
            printer << ", ";
          }
        }
        value->PrettyPrint(component_type, printer);
      }
      printer << '\n';
    }
    printer << ']';
  }
}

void VectorValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitVectorValue(this, for_type);
}

bool TableValue::AddMember(std::string_view name, std::unique_ptr<Value> value) {
  const TableMember* member = table_definition_.GetMember(name);
  if (member == nullptr) {
    return false;
  }
  AddMember(member, std::move(value));
  return true;
}

int TableValue::DisplaySize(const Type* for_type, int remaining_size) const {
  int size = 0;
  for (const auto& member : table_definition_.members()) {
    if ((member != nullptr) && !member->reserved()) {
      auto it = members_.find(member.get());
      if ((it == members_.end()) || it->second->IsNull())
        continue;
      // Two characters for the separator ("{ " or ", "), three characters for " = ".
      size += static_cast<int>(member->name().size()) + 2 + 3;
      // Two characters for ": ".
      size += static_cast<int>(member->type()->Name().size()) + 2;
      size += it->second->DisplaySize(member->type(), remaining_size - size);
      if (size > remaining_size) {
        return size;
      }
    }
  }
  // Two characters for the closing brace (" }").
  size += 2;
  return size;
}

void TableValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  int display_size = DisplaySize(for_type, printer.remaining_size());
  if (display_size == 2) {
    printer << "{}";
  } else if (display_size <= printer.remaining_size()) {
    const char* separator = "{ ";
    for (const auto& member : table_definition_.members()) {
      if ((member != nullptr) && !member->reserved()) {
        auto it = members_.find(member.get());
        if ((it == members_.end()) || it->second->IsNull())
          continue;
        printer << separator << member->name() << ": " << Green << member->type()->Name()
                << ResetColor << " = ";
        separator = ", ";
        it->second->PrettyPrint(member->type(), printer);
      }
    }
    printer << " }";
  } else {
    printer << "{\n";
    {
      Indent indent(printer);
      for (const auto& member : table_definition_.members()) {
        if ((member != nullptr) && !member->reserved()) {
          auto it = members_.find(member.get());
          if ((it == members_.end()) || it->second->IsNull())
            continue;
          std::string type_name = member->type()->Name();
          printer << member->name() << ": " << Green << type_name << ResetColor << " = ";
          it->second->PrettyPrint(member->type(), printer);
          printer << "\n";
        }
      }
    }
    printer << '}';
  }
}

void TableValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitTableValue(this, for_type);
}

}  // namespace fidl_codec
