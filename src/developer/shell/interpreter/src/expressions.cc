// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/expressions.h"

#include <limits>
#include <memory>
#include <ostream>

#include "src/developer/shell/interpreter/src/instructions.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/schema.h"
#include "src/developer/shell/interpreter/src/scope.h"
#include "src/developer/shell/interpreter/src/types.h"

namespace shell {
namespace interpreter {

// - IntegerLiteral --------------------------------------------------------------------------------

void IntegerLiteral::Dump(std::ostream& os) const {
  if (negative_) {
    os << '-';
  }
  os << absolute_value_;
}

std::unique_ptr<Type> IntegerLiteral::InferType(ExecutionContext* context) const {
  uint64_t max_absolute_value = std::numeric_limits<int64_t>::max();
  if (negative_) {
    ++max_absolute_value;
  }
  if (absolute_value_ <= max_absolute_value) {
    return std::make_unique<TypeInt64>();
  }
  return std::make_unique<TypeInteger>();
}

bool IntegerLiteral::Compile(ExecutionContext* context, code::Code* code,
                             const Type* for_type) const {
  return for_type->GenerateIntegerLiteral(context, code, this);
}

// - ObjectField -----------------------------------------------------------------------------------

void ObjectDeclarationField::Dump(std::ostream& os) const {
  std::stringstream type_str;
  field_schema_->type()->Dump(type_str);
  std::string type = type_str.str();
  if (type != "") {
    os << field_schema_->name() << ": " << *(field_schema_->type()) << "(" << *expression_ << ")";
  } else {
    os << field_schema_->name() << ": " << *expression_;
  }
}

// - ObjectDeclaration -----------------------------------------------------------------------------

ObjectDeclaration::ObjectDeclaration(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                                     std::shared_ptr<ObjectSchema> object_schema,
                                     std::vector<std::unique_ptr<ObjectDeclarationField>>&& fields)
    : Expression(interpreter, file_id, node_id),
      object_schema_(std::move(object_schema)),
      fields_(std::move(fields)) {
  // Fields need to be in the same order that they are in the schema.  Find them in the schema and
  // insert them at the correct point.
  for (size_t i = 0; i < object_schema_->fields().size(); i++) {
    if (object_schema_->fields()[i].get() != fields_[i]->schema()) {
      bool found = false;
      for (size_t j = i + 1; j < object_schema_->fields().size(); j++) {
        if (object_schema_->fields()[i].get() == fields_[j]->schema()) {
          ObjectDeclarationField* tmp = fields_[j].release();
          fields_[j].reset(fields_[i].release());
          fields_[i].reset(tmp);
          found = true;
          break;
        }
      }
      FX_DCHECK(found) << "Unable to find schema for field";
    }
  }
}

void ObjectDeclaration::Dump(std::ostream& os) const {
  os << "{";
  const char* separator = "";
  for (size_t i = 0; i < fields_.size(); i++) {
    os << separator << *(fields_[i]);
    separator = ", ";
  }
  os << "}";
}

std::unique_ptr<Type> ObjectDeclaration::InferType(ExecutionContext* context) const {
  return std::make_unique<TypeObject>(object_schema_);
}

bool ObjectDeclaration::Compile(ExecutionContext* context, code::Code* code,
                                const Type* for_type) const {
  const TypeObject* object_type = const_cast<Type*>(for_type)->AsTypeObject();
  object_type->GenerateInitialization(context, code, this);
  object_type->GenerateObject(context, code, this);
  return true;
}

// - StringLiteral ---------------------------------------------------------------------------------

void StringLiteral::Dump(std::ostream& os) const {
  // TODO(vbelliard): escape special characters.
  os << '"' << string()->value() << '"';
}

std::unique_ptr<Type> StringLiteral::InferType(ExecutionContext* context) const {
  return std::make_unique<TypeString>();
}

bool StringLiteral::Compile(ExecutionContext* context, code::Code* code,
                            const Type* for_type) const {
  return for_type->GenerateStringLiteral(context, code, this);
}

// - ExpressionVariable ----------------------------------------------------------------------------

void ExpressionVariable::Dump(std::ostream& os) const { os << name_; }

std::unique_ptr<Type> ExpressionVariable::InferType(ExecutionContext* context) const {
  const Variable* definition = context->interpreter()->SearchGlobal(name_);
  if (definition == nullptr) {
    return std::unique_ptr<TypeUndefined>();
  }
  return definition->type()->Duplicate();
}

bool ExpressionVariable::Compile(ExecutionContext* context, code::Code* code,
                                 const Type* for_type) const {
  const Variable* definition = context->interpreter()->SearchGlobal(name_);
  if (definition == nullptr) {
    context->EmitError(id(), "Can't find variable " + name_ + ".");
    return false;
  }
  return for_type->GenerateVariable(context, code, id(), definition);
}

void ExpressionVariable::Assign(ExecutionContext* context, code::Code* code) const {
  const Variable* definition = context->interpreter()->SearchGlobal(name_);
  if (definition == nullptr) {
    context->EmitError(id(), "Can't find variable " + name_ + ".");
    return;
  }
  if (!definition->is_mutable()) {
    context->EmitError(id(), "Can't assign constant " + name_ + ".");
    return;
  }
  definition->type()->GenerateAssignVariable(context, code, id(), definition);
}

// - Addition --------------------------------------------------------------------------------------

void Addition::Dump(std::ostream& os) const {
  os << *left() << (with_exceptions_ ? " +? " : " + ") << *right();
}

std::unique_ptr<Type> Addition::InferType(ExecutionContext* context) const {
  return left()->IsConstant() ? right()->InferType(context) : left()->InferType(context);
}

bool Addition::Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const {
  return for_type->GenerateAddition(context, code, this);
}

size_t Addition::GenerateStringTerms(ExecutionContext* context, code::Code* code,
                                     const Type* for_type) const {
  return left()->GenerateStringTerms(context, code, for_type) +
         right()->GenerateStringTerms(context, code, for_type);
}

}  // namespace interpreter
}  // namespace shell
