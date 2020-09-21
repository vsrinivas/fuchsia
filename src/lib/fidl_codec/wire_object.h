// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
#define SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_

#include <lib/fidl/cpp/message.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/message_decoder.h"

namespace fidl_codec {

class FidlMessageValue;
class HandleValue;
class StringValue;
class StructValue;
class VectorValue;
class Visitor;

// Base class for all the values we can find within a message.
class Value {
 public:
  Value() = default;
  virtual ~Value() = default;

  virtual bool IsNull() const { return false; }

  // Returns the uint8_t value of the value. If the value is not a uint8_t value this returns zero.
  // This is used to eventually display a vector of uint8_t values as a string.
  virtual uint8_t GetUint8Value() const { return 0; }

  // Gets the integer value of the value. Returns false if the node can't compute an integer value.
  // For floating point values, the floating point value is converted to the nearest integer
  // value.
  virtual bool GetIntegerValue(uint64_t* absolute, bool* negative) const { return false; }

  // Gets the floating point value of the value. Returns false if the node can't compute a floating
  // point value. For integer values, we can lost precision during the conversion.
  virtual bool GetDoubleValue(double* result) const { return false; }

  // Methods to downcast a value.
  virtual const StringValue* AsStringValue() const { return nullptr; }
  virtual const HandleValue* AsHandleValue() const { return nullptr; }
  virtual StructValue* AsStructValue() { return nullptr; }
  virtual const StructValue* AsStructValue() const { return nullptr; }
  virtual const VectorValue* AsVectorValue() const { return nullptr; }
  virtual const FidlMessageValue* AsFidlMessageValue() const { return nullptr; }

  // Returns true if we need to load information about the handle (call to zx_object_get_info with
  // ZX_INFO_HANDLE_TABLE). We need to load information about the handle if one of the handles of
  // the value has an unknown koid.
  virtual bool NeedsToLoadHandleInfo(zx_koid_t tid,
                                     semantic::HandleSemantic* handle_semantic) const {
    return false;
  }

  // Returns the size needed to display the value. If the needed size is
  // greater than |remaining_size|, the return value can be anything greater
  // than |remaining_size| and the only useful information is that the value
  // can't fit in |remaining_size|.
  // Remaining size is just an optimization parameter. It avoids to compute the
  // whole display size for an object: the computation is stopped as soon as we
  // find that the object doesn't fit.
  virtual int DisplaySize(const Type* for_type, int remaining_size) const = 0;

  // Pretty print of the value.
  virtual void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const = 0;

  // Use a visitor on this value;
  virtual void Visit(Visitor* visitor, const Type* for_type) const = 0;
};

// An invalid value. This value can't be present in a valid object.
// It can only be found if we had an error while decoding a message.
class InvalidValue : public Value {
 public:
  InvalidValue() = default;

  int DisplaySize(const Type* for_type, int remaining_size) const override {
    constexpr int kInvalidSize = 7;
    return kInvalidSize;  // length of "invalid"
  }

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override {
    printer << Red << "invalid" << ResetColor;
  }

  void Visit(Visitor* visitor, const Type* for_type) const override;
};

// A null value.
class NullValue : public Value {
 public:
  NullValue() = default;

  bool IsNull() const override { return true; }

  int DisplaySize(const Type* for_type, int remaining_size) const override {
    constexpr int kNullSize = 4;
    return kNullSize;  // length of "null"
  }

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override {
    printer << Red << "null" << ResetColor;
  }

  void Visit(Visitor* visitor, const Type* for_type) const override;
};

// A value with no known representation (we only print the raw data).
class RawValue : public Value {
 public:
  RawValue(const uint8_t* data, size_t size) : data_(data, data + size) {}

  const std::vector<uint8_t>& data() const { return data_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const std::vector<uint8_t> data_;
};

// A Boolean value.
class BoolValue : public Value {
 public:
  explicit BoolValue(uint8_t value) : value_(value) {}

  uint8_t value() const { return value_; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const uint8_t value_;
};

class IntegerValue : public Value {
 public:
  IntegerValue(uint64_t absolute_value, bool negative)
      : absolute_value_(absolute_value), negative_(negative) {}
  explicit IntegerValue(int64_t value)
      : absolute_value_((value < 0) ? -static_cast<uint64_t>(value) : value),
        negative_(value < 0) {}
  explicit IntegerValue(uint64_t value) : absolute_value_(value), negative_(false) {}

  uint64_t absolute_value() const { return absolute_value_; }
  bool negative() const { return negative_; }

  uint8_t GetUint8Value() const override;

  bool GetIntegerValue(uint64_t* absolute, bool* negative) const override {
    *absolute = absolute_value_;
    *negative = negative_;
    return true;
  }

  bool GetDoubleValue(double* result) const override {
    *result = absolute_value_;
    if (negative_) {
      *result = -(*result);
    }
    return true;
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const uint64_t absolute_value_;
  const bool negative_;
};

class DoubleValue : public Value {
 public:
  explicit DoubleValue(double value) : value_(value) {}

  double value() const { return value_; }

  bool GetIntegerValue(uint64_t* absolute, bool* negative) const override {
    if (value_ < 0) {
      *absolute = static_cast<uint64_t>(-value_);
      *negative = true;
    } else {
      *absolute = static_cast<uint64_t>(value_);
      *negative = false;
    }
    return true;
  }

  bool GetDoubleValue(double* result) const override {
    *result = value_;
    return true;
  }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const double value_;
};

// A string value.
class StringValue : public Value {
 public:
  explicit StringValue(std::string_view string) : string_(string) {}

  const std::string& string() const { return string_; }

  const StringValue* AsStringValue() const override { return this; }

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const std::string string_;
};

// A handle.
class HandleValue : public Value {
 public:
  explicit HandleValue(const zx_handle_info_t& handle) : handle_(handle) {}

  const zx_handle_info_t& handle() const { return handle_; }

  const HandleValue* AsHandleValue() const override { return this; }

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const zx_handle_info_t handle_;
};

// An union.
class UnionValue : public Value {
 public:
  UnionValue(const UnionMember& member, std::unique_ptr<Value> value)
      : member_(member), value_(std::move(value)) {}

  const UnionMember& member() const { return member_; }
  const std::unique_ptr<Value>& value() const { return value_; }

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const UnionMember& member_;
  const std::unique_ptr<Value> value_;
};

// An instance of a Struct. This includes requests and responses which are also structs.
class StructValue : public Value {
 public:
  explicit StructValue(const Struct& struct_definition) : struct_definition_(struct_definition) {}

  const Struct& struct_definition() const { return struct_definition_; }
  const std::map<const StructMember*, std::unique_ptr<Value>>& fields() const { return fields_; }

  void AddField(const StructMember* member, std::unique_ptr<Value> value) {
    fields_.emplace(std::make_pair(member, std::move(value)));
  }

  void inline AddField(std::string_view name, std::unique_ptr<Value> value) {
    AddField(name, 0, std::move(value));
  }

  void AddField(std::string_view name, uint32_t id, std::unique_ptr<Value> value);

  StructValue* AsStructValue() override { return this; }
  const StructValue* AsStructValue() const override { return this; }

  std::pair<const Type*, const Value*> GetFieldValue(std::string_view field_name) const;

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

  // Extract the JSON for this object.
  void ExtractJson(rapidjson::Document::AllocatorType& allocator, rapidjson::Value& result) const;

 private:
  const Struct& struct_definition_;
  std::map<const StructMember*, std::unique_ptr<Value>> fields_;
};

// A vector.
class VectorValue : public Value {
 public:
  VectorValue() = default;

  const std::vector<std::unique_ptr<Value>>& values() const { return values_; }

  void AddValue(std::unique_ptr<Value> value) {
    if (value == nullptr) {
      is_string_ = false;
    } else {
      uint8_t uvalue = value->GetUint8Value();
      if (!std::isprint(uvalue)) {
        if ((uvalue == '\r') || (uvalue == '\n')) {
          has_new_line_ = true;
        } else {
          is_string_ = false;
        }
      }
    }
    values_.push_back(std::move(value));
  }

  const VectorValue* AsVectorValue() const override { return this; }

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  std::vector<std::unique_ptr<Value>> values_;
  bool is_string_ = true;
  bool has_new_line_ = false;
};

// A table.
class TableValue : public Value {
 public:
  explicit TableValue(const Table& table_definition) : table_definition_(table_definition) {}

  const Table& table_definition() const { return table_definition_; }
  const std::map<const TableMember*, std::unique_ptr<Value>>& members() const { return members_; }
  Ordinal32 highest_member() const { return highest_member_; }

  void AddMember(const TableMember* member, std::unique_ptr<Value> value) {
    members_.emplace(std::make_pair(member, std::move(value)));
    if (member->ordinal() > highest_member_) {
      highest_member_ = member->ordinal();
    }
  }

  bool AddMember(std::string_view name, std::unique_ptr<Value> value);

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  const Table& table_definition_;
  std::map<const TableMember*, std::unique_ptr<Value>> members_;
  Ordinal32 highest_member_ = 0;
};

// An instance of a FIDL message.
class FidlMessageValue : public Value {
 public:
  FidlMessageValue(fidl_codec::DecodedMessage* message, std::string global_errors,
                   const uint8_t* bytes, uint32_t num_bytes, const zx_handle_info_t* handles,
                   uint32_t num_handles);
  FidlMessageValue(zx_txid_t txid, uint64_t ordinal, const std::string& global_errors,
                   const std::string& epitaph_error, bool received, bool is_request,
                   bool unknown_direction, const fidl_codec::InterfaceMethod* method,
                   const uint8_t* bytes, size_t byte_size, const std::string& request_errors,
                   const std::string& response_errors)
      : txid_(txid),
        ordinal_(ordinal),
        global_errors_(global_errors),
        epitaph_error_(epitaph_error),
        received_(received),
        is_request_(is_request),
        unknown_direction_(unknown_direction),
        method_(method),
        bytes_(bytes, bytes + byte_size),
        request_errors_(request_errors),
        response_errors_(response_errors) {}

  zx_txid_t txid() const { return txid_; }
  uint64_t ordinal() const { return ordinal_; }
  const std::string& global_errors() const { return global_errors_; }
  const std::string& epitaph_error() const { return epitaph_error_; }
  bool received() const { return received_; }
  bool is_request() const { return is_request_; }
  bool unknown_direction() const { return unknown_direction_; }
  const fidl_codec::InterfaceMethod* method() const { return method_; }
  const std::vector<uint8_t> bytes() const { return bytes_; }
  const std::vector<zx_handle_info_t> handles() const { return handles_; }
  void add_handle(const zx_handle_info_t& handle) { handles_.emplace_back(handle); }
  const StructValue* decoded_request() const { return decoded_request_.get(); }
  void set_decoded_request(std::unique_ptr<StructValue> decoded_request) {
    decoded_request_ = std::move(decoded_request);
  }
  const std::string& request_errors() const { return request_errors_; }
  const StructValue* decoded_response() const { return decoded_response_.get(); }
  void set_decoded_response(std::unique_ptr<StructValue> decoded_response) {
    decoded_response_ = std::move(decoded_response);
  }
  const std::string& response_errors() const { return response_errors_; }
  bool matched_request() const { return (decoded_request_ != nullptr) && request_errors_.empty(); }
  bool matched_response() const {
    return (decoded_response_ != nullptr) && response_errors_.empty();
  }

  const FidlMessageValue* AsFidlMessageValue() const override { return this; }

  bool NeedsToLoadHandleInfo(zx_koid_t tid,
                             semantic::HandleSemantic* handle_semantic) const override;

  int DisplaySize(const Type* for_type, int remaining_size) const override;

  void PrettyPrint(const Type* for_type, PrettyPrinter& printer) const override;

  void PrintMessage(PrettyPrinter& printer) const;

  void PrintMessageBody(PrettyPrinter& printer) const;

  void DumpMessage(PrettyPrinter& printer) const;

  void Visit(Visitor* visitor, const Type* for_type) const override;

 private:
  // The transfer ID of the mesage.
  const zx_txid_t txid_;
  // The ordinal of the message.
  const uint64_t ordinal_;
  // Global errors for the message (errors before we can start decoding anything).
  const std::string global_errors_;
  // Text value of the error status of the epitaph.
  const std::string epitaph_error_;
  // True if the message was received.
  const bool received_;
  // True if the message is a request. False if the message is a response.
  const bool is_request_;
  // True if we haven't been able to select a request of a response (case where both can be
  // decoded).
  const bool unknown_direction_;
  // The method associated with the ordinal.
  const fidl_codec::InterfaceMethod* const method_;
  // All the bytes of the message.
  std::vector<uint8_t> bytes_;
  // All the handles of the message.
  std::vector<zx_handle_info_t> handles_;
  // Value of the request we have been able to decode.
  std::unique_ptr<StructValue> decoded_request_;
  // Errors generated during the decoding of the request. If not empty, decoded_request_ holds only
  // a partial result.
  const std::string request_errors_;
  // Value of the response we have been able to decode.
  std::unique_ptr<StructValue> decoded_response_;
  // Errors generated during the decoding of the response. If not empty, decoded_response_ holds
  // only a partial result.
  const std::string response_errors_;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_OBJECT_H_
