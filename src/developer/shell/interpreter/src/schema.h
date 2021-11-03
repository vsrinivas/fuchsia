// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>
#include <vector>

#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/nodes.h"

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_

namespace shell::interpreter {

class ObjectFieldSchema : public Node {
 public:
  ObjectFieldSchema(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                    const std::string_view& name, std::unique_ptr<Type> type)
      : Node(interpreter, file_id, node_id), name_(name), type_(std::move(type)) {}

  // Prints the expression.
  virtual void Dump(std::ostream& os) const {}

  const Type* type() const { return type_.get(); }

  const std::string& name() { return name_; }

  void set_offset(size_t offset) { offset_ = offset; }

  size_t offset() const { return offset_; }

 private:
  std::string name_;
  std::unique_ptr<Type> type_;

  // The offset of fields with this schema, in bytes.
  size_t offset_;
};

class ObjectSchema : public Node {
 public:
  ObjectSchema(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
               std::vector<std::shared_ptr<ObjectFieldSchema>>&& fields);
  virtual ~ObjectSchema();

  const std::vector<std::shared_ptr<ObjectFieldSchema>>& fields() const { return fields_; }

  // Prints the expression.
  virtual void Dump(std::ostream& os) const {}

  static std::unique_ptr<Type> GetType(std::shared_ptr<ObjectSchema> schema);

  // Allocates enough space for an object with the given |schema|.  Objects have enough space after
  // them to contain an instance of the object with the given |schema|.
  static Object* AllocateObject(std::shared_ptr<ObjectSchema> schema);

 private:
  std::vector<std::shared_ptr<ObjectFieldSchema>> fields_;

  // size in bytes of the object, including space for the initial Object instance, and with no
  // padding at the end.
  size_t size_;

  // The size of the allocated object, including the size of the header Object, padding, and enough
  // room for the values.
  size_t AllocationSize();
};

}  // namespace shell::interpreter

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCHEMA_H_
