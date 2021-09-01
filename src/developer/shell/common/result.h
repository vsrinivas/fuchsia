// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_COMMON_RESULT_H_
#define SRC_DEVELOPER_SHELL_COMMON_RESULT_H_

#include <lib/fidl/llcpp/vector_view.h>

#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "fidl/fuchsia.shell/cpp/wire.h"

namespace shell::common {

// Base class for all the types.
class ResultType {
 public:
  ResultType() = default;
  virtual ~ResultType() = default;

  // Dumps the type.
  virtual void Dump(std::ostream& os, const char* separator) const {}
};

class ResultTypeUint64 : public ResultType {
 public:
  ResultTypeUint64() = default;

  void Dump(std::ostream& os, const char* separator) const override { os << separator << "uint64"; }
};

class ResultTypeString : public ResultType {
 public:
  ResultTypeString() = default;

  void Dump(std::ostream& os, const char* separator) const override { os << separator << "string"; }
};

// Defines a field for a result object schema.
class ResultSchemaField {
 public:
  ResultSchemaField(uint64_t node_id, const fidl::StringView& name,
                    std::unique_ptr<ResultType> type)
      : node_id_(node_id), name_(name.data(), name.size()), type_(std::move(type)) {}

  uint64_t node_id() const { return node_id_; }
  const std::string& name() const { return name_; }
  const ResultType* type() const { return type_.get(); }

 private:
  const uint64_t node_id_;
  const std::string name_;
  std::unique_ptr<ResultType> type_;
};

// Defines an object schema for a result.
class ResultSchema {
 public:
  ResultSchema() = default;

  void AddField(uint64_t node_id, const fidl::StringView& name, std::unique_ptr<ResultType> type) {
    fields_.emplace_back(std::make_shared<ResultSchemaField>(node_id, name, std::move(type)));
  }

  std::shared_ptr<ResultSchemaField> SearchField(uint64_t field_id) const {
    for (const auto& field : fields_) {
      if (field->node_id() == field_id) {
        return field;
      }
    }
    return nullptr;
  }

 private:
  std::vector<std::shared_ptr<ResultSchemaField>> fields_;
};

// Defines an object type for a result.
class ResultTypeObject : public ResultType {
 public:
  ResultTypeObject(std::shared_ptr<ResultSchema> schema) : schema_(schema) {}

 private:
  std::shared_ptr<ResultSchema> schema_;
};

// Base class for a result.
class ResultNode {
 public:
  ResultNode() = default;
  virtual ~ResultNode() = default;

  // Dumps the result.
  virtual void Dump(std::ostream& os) const = 0;
};

// Defines an integer literal result.
class ResultNodeIntegerLiteral : public ResultNode {
 public:
  ResultNodeIntegerLiteral(const fidl::VectorView<uint64_t>& absolute_value, bool negative)
      : negative_(negative) {
    for (auto value : absolute_value) {
      absolute_value_.push_back(value);
    }
  }

  void Dump(std::ostream& os) const override {
    if (negative_) {
      os << '-';
    }
    if (absolute_value_.empty()) {
      os << '0';
    } else if (absolute_value_.size() == 1) {
      os << absolute_value_[0];
    } else {
      os << "???";
    }
  }

 private:
  std::vector<uint64_t> absolute_value_;
  const bool negative_;
};

// Define a string literal result.
class ResultNodeStringLiteral : public ResultNode {
 public:
  ResultNodeStringLiteral(const fidl::StringView& string) : string_(string.data(), string.size()) {}

  void Dump(std::ostream& os) const override { os << '"' << string_ << '"'; }

 private:
  const std::string string_;
};

// Defines a field for an object result.
class ResultNodeObjectField {
 public:
  ResultNodeObjectField(std::shared_ptr<ResultSchemaField> field, std::unique_ptr<ResultNode> value)
      : field_(field), value_(std::move(value)) {}

  void Dump(std::ostream& os) const {
    os << field_->name() << ": ";

    if (field_->type() != nullptr) {
      field_->type()->Dump(os, "");
      os << "(";
      value_->Dump(os);
      os << ")";
    } else {
      value_->Dump(os);
    }
  }

 private:
  std::shared_ptr<ResultSchemaField> field_;
  std::unique_ptr<ResultNode> value_;
};

// Defines an object result.
class ResultNodeObject : public ResultNode {
 public:
  ResultNodeObject(std::shared_ptr<ResultSchema> schema) : schema_(schema) {}

  void AddField(std::shared_ptr<ResultSchemaField> field, std::unique_ptr<ResultNode> value) {
    fields_.emplace_back(std::make_unique<ResultNodeObjectField>(field, std::move(value)));
  }

  void Dump(std::ostream& os) const override {
    os << '{';
    const char* separator = "";
    for (const auto& field : fields_) {
      os << separator;
      field->Dump(os);
      separator = ", ";
    }
    os << '}';
  }

 private:
  std::shared_ptr<ResultSchema> schema_;
  std::vector<std::unique_ptr<ResultNodeObjectField>> fields_;
};

// Helper for a result deserialization from a vector of nodes.
class DeserializeResult {
 public:
  DeserializeResult() = default;

  // Deserializes a result from a vector of nodes.
  std::unique_ptr<ResultNode> Deserialize(const fidl::VectorView<fuchsia_shell::wire::Node>& nodes);

  // Deserializes a node (value).
  std::unique_ptr<ResultNode> DeserializeNode(
      const fidl::VectorView<fuchsia_shell::wire::Node>& nodes, uint64_t node_id);

  // Deserializes an object schema.
  std::shared_ptr<ResultSchema> DeserializeSchema(
      const fidl::VectorView<fuchsia_shell::wire::Node>& nodes, uint64_t node_id);

  // Deserializes a type.
  std::unique_ptr<ResultType> DeserializeType(
      const fidl::VectorView<fuchsia_shell::wire::Node>& nodes,
      const fuchsia_shell::wire::ShellType& shell_type);

 private:
  // All the schema which have already been deserialized.
  std::map<uint64_t, std::shared_ptr<ResultSchema>> schemas_;
};

}  // namespace shell::common

#endif  // SRC_DEVELOPER_SHELL_COMMON_RESULT_H_
