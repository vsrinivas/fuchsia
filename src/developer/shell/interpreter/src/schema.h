// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_

#include "src/developer/shell/interpreter/src/nodes.h"

namespace shell::interpreter {

class ObjectFieldSchema : public Schema {
 public:
  ObjectFieldSchema(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                    const std::string_view& name, std::unique_ptr<Type> type)
      : Schema(interpreter, file_id, node_id), name_(name), type_(std::move(type)) {}

  virtual ObjectFieldSchema* AsObjectFieldSchema() override { return this; }

  // Prints the expression.
  virtual void Dump(std::ostream& os) const {};

  const Type* type() { return type_.get(); }

  const std::string& name() { return name_; }

 private:
  std::string name_;
  std::unique_ptr<Type> type_;
};

class ObjectSchema : public Schema {
 public:
  ObjectSchema(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
               std::vector<std::unique_ptr<ObjectFieldSchema>>&& fields)
      : Schema(interpreter, file_id, node_id), fields_(std::move(fields)) {}

  virtual ObjectSchema* AsObjectSchema() override { return this; }

  const std::vector<std::unique_ptr<ObjectFieldSchema>>& fields() const { return fields_; }

  // Prints the expression.
  virtual void Dump(std::ostream& os) const {};

  std::unique_ptr<Type> GetType() const;

 private:
  std::vector<std::unique_ptr<ObjectFieldSchema>> fields_;
};

}  // namespace shell::interpreter

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_
