// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/schema.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include <algorithm>
#include <ostream>

#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/types.h"

namespace shell::interpreter {

ObjectSchema::ObjectSchema(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                           std::vector<std::shared_ptr<ObjectFieldSchema>>&& fields)
    : Node(interpreter, file_id, node_id), fields_(std::move(fields)) {
  interpreter->increment_object_schema_count();
  // Assume the start of the data object is 8-byte-aligned.
  size_t curr_offset = (sizeof(Object) + 7) & ~7;
  for (auto& field : fields_) {
    size_t alignment_factor = field->type()->Alignment() - 1;
    FX_DCHECK((field->type()->Alignment() & alignment_factor) == 0)
        << "Field alignment is not a power of two";

    curr_offset = (curr_offset + alignment_factor) & ~alignment_factor;
    field->set_offset(curr_offset);
    curr_offset += field->type()->Size();
  }

  struct SchemaLT {
    bool operator()(std::shared_ptr<ObjectFieldSchema>& lhs,
                    std::shared_ptr<ObjectFieldSchema>& rhs) const {
      return lhs->name() < rhs->name();
    }
  } schema_lt;
  std::sort(fields.begin(), fields.end(), schema_lt);

  size_ = curr_offset;
}

ObjectSchema::~ObjectSchema() { interpreter()->decrement_object_schema_count(); }

std::unique_ptr<Type> ObjectSchema::GetType(std::shared_ptr<ObjectSchema> schema) {
  return std::make_unique<TypeObject>(schema);
}

Object* ObjectSchema::AllocateObject(std::shared_ptr<ObjectSchema> schema) {
  size_t size = schema->AllocationSize();
  uint8_t* buf = new uint8_t[size]();
  return new (buf) Object(schema->interpreter(), schema);
}

size_t ObjectSchema::AllocationSize() { return ((sizeof(Object) + 7) & ~7) + size_; }

}  // namespace shell::interpreter
