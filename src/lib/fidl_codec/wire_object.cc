// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/wire_object.h"

#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/json_visitor.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/printer.h"
#include "src/lib/fidl_codec/status.h"
#include "src/lib/fidl_codec/visitor.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

std::string DocumentToString(rapidjson::Document* document) {
  rapidjson::StringBuffer output;
  rapidjson::Writer<rapidjson::StringBuffer> writer(output);
  document->Accept(writer);
  return output.GetString();
}

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
  std::stringstream dummyStream;
  auto dummyPrinter = PrettyPrinter(dummyStream, WithoutColors, true, "", INT_MAX, false);
  for_type->PrettyPrint(this, dummyPrinter);
  return dummyStream.str().length();
}

void IntegerValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  FX_DCHECK(for_type != nullptr);
  for_type->PrettyPrint(this, printer);
}

void IntegerValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitIntegerValue(this, for_type);
}

int DoubleValue::DisplaySize(const Type* for_type, int /*remaining_size*/) const {
  return std::to_string(value_).size();
}

void DoubleValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  FX_DCHECK(for_type != nullptr);
  for_type->PrettyPrint(this, printer);
}

void DoubleValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitDoubleValue(this, for_type);
}

int StringValue::DisplaySize(const Type* /*for_type*/, int /*remaining_size*/) const {
  return static_cast<int>(string_.size()) + 2;  // The two quotes.
}

void StringValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  printer.DisplayString(string_);
}

void StringValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitStringValue(this, for_type);
}

bool HandleValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                        semantic::HandleSemantic* handle_semantic) const {
  if (handle_.handle == ZX_HANDLE_INVALID) {
    return false;
  }
  return handle_semantic->NeedsToLoadHandleInfo(tid, handle_.handle);
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

bool UnionValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                       semantic::HandleSemantic* handle_semantic) const {
  return value_->NeedsToLoadHandleInfo(tid, handle_semantic);
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
    printer << "{ " << member_.name() << ": ";
    member_.type()->PrettyPrint(printer);
    printer << " = ";
    value_->PrettyPrint(member_.type(), printer);
    printer << " }";
  } else {
    printer << "{\n";
    {
      Indent indent(printer);
      printer << member_.name() << ": ";
      member_.type()->PrettyPrint(printer);
      printer << " = ";
      value_->PrettyPrint(member_.type(), printer);
      printer << '\n';
    }
    printer << "}";
  }
}

void UnionValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitUnionValue(this, for_type);
}

std::pair<const Type*, const Value*> StructValue::GetFieldValue(std::string_view field_name) const {
  for (const auto& field : fields_) {
    if (field.first->name() == field_name) {
      return std::make_pair(field.first->type(), field.second.get());
    }
  }
  return std::make_pair(nullptr, nullptr);
}

void StructValue::AddField(std::string_view name, uint32_t id, std::unique_ptr<Value> value) {
  StructMember* member = struct_definition_.SearchMember(name, id);
  if (member != nullptr) {
    AddField(member, std::move(value));
  }
}

bool StructValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                        semantic::HandleSemantic* handle_semantic) const {
  for (const auto& field : fields_) {
    if (field.second->NeedsToLoadHandleInfo(tid, handle_semantic)) {
      return true;
    }
  }
  return false;
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
      printer << separator << member->name() << ": ";
      member->type()->PrettyPrint(printer);
      printer << " = ";
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
        printer << member->name() << ": ";
        member->type()->PrettyPrint(printer);
        printer << " = ";
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

bool VectorValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                        semantic::HandleSemantic* handle_semantic) const {
  for (const auto& value : values_) {
    if (value->NeedsToLoadHandleInfo(tid, handle_semantic)) {
      return true;
    }
  }
  return false;
}

int VectorValue::DisplaySize(const Type* for_type, int remaining_size) const {
  FX_DCHECK(for_type != nullptr);
  if (values_.empty()) {
    return 2;  // The two brackets.
  }
  if (is_string_) {
    return static_cast<int>(values_.size() + 2);  // The string and the two quotes.
  }
  const Type* component_type = for_type->GetComponentType();
  FX_DCHECK(component_type != nullptr);
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
  FX_DCHECK(for_type != nullptr);
  if (values_.empty()) {
    printer << "[]";
  } else if (is_string_) {
    if (has_new_line_) {
      printer << "[\n";
      {
        Indent indent(printer);
        printer << Red;
        for (const auto& value : values_) {
          printer << static_cast<char>(value->GetUint8Value());
        }
        printer << ResetColor;
      }
      printer << '\n';
      printer << ']';
    } else {
      printer << Red << '"';
      for (const auto& value : values_) {
        printer << static_cast<char>(value->GetUint8Value());
      }
      printer << '"' << ResetColor;
    }
  } else if (DisplaySize(for_type, printer.remaining_size()) <= printer.remaining_size()) {
    const Type* component_type = for_type->GetComponentType();
    FX_DCHECK(component_type != nullptr);
    const char* separator = "[ ";
    for (const auto& value : values_) {
      printer << separator;
      separator = ", ";
      value->PrettyPrint(component_type, printer);
    }
    printer << " ]";
  } else {
    const Type* component_type = for_type->GetComponentType();
    FX_DCHECK(component_type != nullptr);
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

bool TableValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                       semantic::HandleSemantic* handle_semantic) const {
  for (const auto& member : members_) {
    if (member.second->NeedsToLoadHandleInfo(tid, handle_semantic)) {
      return true;
    }
  }
  return false;
}

int TableValue::DisplaySize(const Type* for_type, int remaining_size) const {
  int size = 0;
  for (const auto& member : table_definition_.members()) {
    if ((member != nullptr) && !member->reserved()) {
      auto it = members_.find(member.get());
      if ((it == members_.end()) || (it->second == nullptr) || it->second->IsNull())
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
        if ((it == members_.end()) || (it->second == nullptr) || it->second->IsNull())
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
          if ((it == members_.end()) || (it->second == nullptr) || it->second->IsNull())
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

FidlMessageValue::FidlMessageValue(fidl_codec::DecodedMessage* message, std::string global_errors,
                                   const uint8_t* bytes, uint32_t num_bytes,
                                   const zx_handle_info_t* handles, uint32_t num_handles)
    : txid_(message->txid()),
      ordinal_(message->ordinal()),
      global_errors_(std::move(global_errors)),
      epitaph_error_(
          (message->ordinal() == kFidlOrdinalEpitaph) ? StatusName(message->epitaph_error()) : ""),
      received_(message->received()),
      is_request_(message->is_request()),
      unknown_direction_(message->direction() == Direction::kUnknown),
      method_(message->method()),
      bytes_(bytes, bytes + num_bytes),
      handles_(handles, handles + num_handles),
      decoded_request_((unknown_direction_ || is_request_) ? std::move(message->decoded_request())
                                                           : nullptr),
      request_errors_((unknown_direction_ || is_request_) ? message->request_error_stream().str()
                                                          : ""),
      decoded_response_(
          (unknown_direction_ || !is_request_) ? std::move(message->decoded_response()) : nullptr),
      response_errors_((unknown_direction_ || !is_request_) ? message->response_error_stream().str()
                                                            : "") {}

bool FidlMessageValue::NeedsToLoadHandleInfo(zx_koid_t tid,
                                             semantic::HandleSemantic* handle_semantic) const {
  for (const auto& handle : handles_) {
    auto inferred_handle_info = handle_semantic->GetInferredHandleInfo(tid, handle.handle);
    if (inferred_handle_info == nullptr) {
      return true;
    }
  }
  return false;
}

int FidlMessageValue::DisplaySize(const Type* for_type, int remaining_size) const { return 0; }

void FidlMessageValue::PrettyPrint(const Type* for_type, PrettyPrinter& printer) const {
  PrintMessage(printer);
  DumpMessage(printer);
}

void FidlMessageValue::PrintMessage(PrettyPrinter& printer) const {
  if (!global_errors_.empty()) {
    printer << global_errors_;
    return;
  }
  if (ordinal_ == kFidlOrdinalEpitaph) {
    printer << fidl_codec::WhiteOnMagenta << (received_ ? "received" : "sent") << " epitaph"
            << fidl_codec::ResetColor << ' ' << fidl_codec::Red << epitaph_error_
            << fidl_codec::ResetColor << "\n";
    return;
  }

  if (unknown_direction_) {
    bool matched_request = (decoded_request_ != nullptr) && request_errors_.empty();
    bool matched_response = (decoded_response_ != nullptr) && response_errors_.empty();
    if (matched_request || matched_response) {
      printer << fidl_codec::Red << "Can't determine request/response." << fidl_codec::ResetColor
              << " it can be:\n";
      fidl_codec::Indent indent(printer);
      PrintMessageBody(printer);
      return;
    }
  }
  PrintMessageBody(printer);
}

void FidlMessageValue::PrintMessageBody(PrettyPrinter& printer) const {
  if (matched_request() && (is_request_ || unknown_direction_)) {
    printer << fidl_codec::WhiteOnMagenta << (received_ ? "received" : "sent") << " request"
            << fidl_codec::ResetColor << ' ' << fidl_codec::Green
            << method_->enclosing_interface().name() << '.' << method_->name()
            << fidl_codec::ResetColor << " = ";
    if (printer.pretty_print()) {
      decoded_request_->PrettyPrint(nullptr, printer);
      printer << '\n';
    } else {
      rapidjson::Document actual_request;
      if (decoded_request_ != nullptr) {
        decoded_request_->ExtractJson(actual_request.GetAllocator(), actual_request);
      }
      printer << DocumentToString(&actual_request) << '\n';
    }
  }
  if (matched_response() && (!is_request_ || unknown_direction_)) {
    printer << fidl_codec::WhiteOnMagenta << (received_ ? "received" : "sent")
            << ((method_->request() != nullptr) ? " response" : " event") << fidl_codec::ResetColor
            << ' ' << fidl_codec::Green << method_->enclosing_interface().name() << '.'
            << method_->name() << fidl_codec::ResetColor << " = ";
    if (printer.pretty_print()) {
      decoded_response_->PrettyPrint(nullptr, printer);
      printer << '\n';
    } else {
      rapidjson::Document actual_response;
      if (decoded_response_ != nullptr) {
        decoded_response_->ExtractJson(actual_response.GetAllocator(), actual_response);
      }
      printer << DocumentToString(&actual_response) << '\n';
    }
  }
  if (matched_request() || matched_response()) {
    return;
  }
  if (!request_errors_.empty()) {
    printer << fidl_codec::Red << (received_ ? "received" : "sent") << " request errors"
            << fidl_codec::ResetColor << ":\n";
    {
      Indent indent(printer);
      printer << request_errors_;
    }
    if (decoded_request_ != nullptr) {
      printer << fidl_codec::WhiteOnMagenta << (received_ ? "received" : "sent") << " request"
              << fidl_codec::ResetColor << ' ' << fidl_codec::Green
              << method_->enclosing_interface().name() << '.' << method_->name()
              << fidl_codec::ResetColor << " = ";
      decoded_request_->PrettyPrint(nullptr, printer);
      printer << '\n';
    }
  }
  if (!response_errors_.empty()) {
    printer << fidl_codec::Red << (received_ ? "received" : "sent")
            << ((method_->request() != nullptr) ? " response errors" : " event errors")
            << fidl_codec::ResetColor << ":\n";
    {
      Indent indent(printer);
      printer << response_errors_;
    }
    if (decoded_response_ != nullptr) {
      printer << fidl_codec::WhiteOnMagenta << (received_ ? "received" : "sent")
              << ((method_->request() != nullptr) ? " response" : " event")
              << fidl_codec::ResetColor << ' ' << fidl_codec::Green
              << method_->enclosing_interface().name() << '.' << method_->name()
              << fidl_codec::ResetColor << " = ";
      decoded_response_->PrettyPrint(nullptr, printer);
      printer << '\n';
    }
  }
}

void FidlMessageValue::DumpMessage(PrettyPrinter& printer) const {
  constexpr int kPatternColorSize = 4;
  constexpr int kPatternSize = 8;
  constexpr int kLineSize = 16;
  constexpr int kLineHandleSize = 4;
  constexpr int kCharactersPerByte = 2;
  if ((decoded_request_ == nullptr) && (decoded_response_ == nullptr) &&
      (ordinal_ != kFidlOrdinalEpitaph)) {
    printer << fidl_codec::Red << "Can't decode message: ";
  } else if (printer.DumpMessages()) {
    printer << "Message: ";
  } else {
    return;
  }
  printer << "num_bytes=" << bytes_.size() << " num_handles=" << handles_.size();
  if (txid_ != 0) {
    printer << " txid=" << std::hex << txid_ << std::dec;
  }
  if (ordinal_ != 0) {
    printer << " ordinal=" << std::hex << ordinal_ << std::dec;
    if (method_ != nullptr) {
      printer << '(' << method_->enclosing_interface().name() << '.' << method_->name() << ')';
    }
  }
  printer << fidl_codec::ResetColor << '\n';
  {
    Indent indent_data(printer);
    printer << "data=";
    const char* separator = "";
    Indent indent_dump(printer);
    for (uint32_t i = 0; i < bytes_.size(); ++i) {
      // Display 16 bytes per line.
      if (i % kLineSize == 0) {
        std::vector<char> buffer(sizeof(uint32_t) * kCharactersPerByte + 1);
        snprintf(buffer.data(), buffer.size(), "%04x", i);
        printer << separator << "\n";
        printer << buffer.data() << ": ";
        separator = "";
      }
      // Display 4 bytes in red then four bytes in black ...
      if (i % kPatternSize == 0) {
        printer << fidl_codec::Red;
      } else if (i % kPatternColorSize == 0) {
        printer << fidl_codec::ResetColor;
      }
      std::vector<char> buffer(sizeof(uint8_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%02x", bytes_[i]);
      printer << separator << buffer.data();
      separator = ", ";
    }
    printer << fidl_codec::ResetColor << '\n';
  }
  if (handles_.size() > 0) {
    Indent indent_data(printer);
    printer << "handles=";
    Indent indent_dump(printer);
    const char* separator = "";
    for (uint32_t i = 0; i < handles_.size(); ++i) {
      // Display 4 bytes per line.
      if (i % kLineHandleSize == 0) {
        std::vector<char> buffer(sizeof(uint32_t) * kCharactersPerByte + 1);
        snprintf(buffer.data(), buffer.size(), "%04x", i);
        printer << separator << "\n";
        printer << buffer.data() << ": ";
        separator = "";
      }
      std::vector<char> buffer(sizeof(zx_handle_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%08x", handles_[i].handle);
      printer << separator << buffer.data();
      separator = ", ";
    }
    printer << '\n';
  }
}

void FidlMessageValue::Visit(Visitor* visitor, const Type* for_type) const {
  visitor->VisitFidlMessageValue(this, for_type);
}

}  // namespace fidl_codec
