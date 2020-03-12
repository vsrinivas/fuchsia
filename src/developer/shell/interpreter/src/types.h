// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_

#include <limits>
#include <utility>

#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

class TypeUndefined : public Type {
 public:
  TypeUndefined() = default;

  size_t Size() const override { return 0; }

  TypeKind Kind() const override { return TypeKind::kUndefined; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

// Base class for all the builtin types (the types known by the interpreter).
class TypeBuiltin : public Type {
 public:
  TypeBuiltin() = default;

  Variable* CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                           const std::string& name) const override;
};

// Base class for all types which can be loaded/stored without any extra operation.
class TypeRaw : public TypeBuiltin {
 public:
  TypeRaw() = default;

  void GenerateDefaultValue(ExecutionContext* context, code::Code* code) const override;

  bool GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                        const Variable* variable) const override;
};

// Base class for all types which are reference counted (which means that the interpreter has to
// call Use/Release when loading/storing).
class TypeReferenceCounted : public TypeBuiltin {
 public:
  TypeReferenceCounted() = default;
};

class TypeBool : public TypeRaw {
 public:
  TypeBool() = default;

  size_t Size() const override { return sizeof(bool); }

  TypeKind Kind() const override { return TypeKind::kBool; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeChar : public TypeRaw {
 public:
  TypeChar() = default;

  size_t Size() const override { return sizeof(uint32_t); }

  TypeKind Kind() const override { return TypeKind::kChar; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeString : public TypeReferenceCounted {
 public:
  TypeString() = default;

  size_t Size() const override { return sizeof(String*); }

  TypeKind Kind() const override { return TypeKind::kString; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void GenerateDefaultValue(ExecutionContext* context, code::Code* code) const override;

  bool GenerateStringLiteral(ExecutionContext* context, code::Code* code,
                             const StringLiteral* literal) const override;

  bool GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                        const Variable* variable) const override;

  bool GenerateAddition(ExecutionContext* context, code::Code* code,
                        const Addition* addition) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInt : public TypeRaw {
 public:
  TypeInt() = default;

  virtual std::pair<uint64_t, uint64_t> Limits() const = 0;

  virtual bool Signed() const = 0;

  bool GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                              const IntegerLiteral* literal) const override;

  bool GenerateAddition(ExecutionContext* context, code::Code* code,
                        const Addition* addition) const override;
};

class TypeSignedInt : public TypeInt {
 public:
  TypeSignedInt() = default;

  bool Signed() const override { return true; }
};

class TypeUnsignedInt : public TypeInt {
 public:
  TypeUnsignedInt() = default;

  bool Signed() const override { return false; }
};

class TypeInt8 : public TypeSignedInt {
 public:
  TypeInt8() = default;

  size_t Size() const override { return sizeof(int8_t); }

  TypeKind Kind() const override { return TypeKind::kInt8; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(static_cast<uint64_t>(std::numeric_limits<int8_t>::max()) + 1,
                          std::numeric_limits<int8_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeUint8 : public TypeUnsignedInt {
 public:
  TypeUint8() = default;

  size_t Size() const override { return sizeof(uint8_t); }

  TypeKind Kind() const override { return TypeKind::kUint8; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(0, std::numeric_limits<uint8_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInt16 : public TypeSignedInt {
 public:
  TypeInt16() = default;

  size_t Size() const override { return sizeof(int16_t); }

  TypeKind Kind() const override { return TypeKind::kInt16; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(static_cast<uint64_t>(std::numeric_limits<int16_t>::max()) + 1,
                          std::numeric_limits<int16_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeUint16 : public TypeUnsignedInt {
 public:
  TypeUint16() = default;

  size_t Size() const override { return sizeof(uint16_t); }

  TypeKind Kind() const override { return TypeKind::kUint16; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(0, std::numeric_limits<uint16_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInt32 : public TypeSignedInt {
 public:
  TypeInt32() = default;

  size_t Size() const override { return sizeof(int32_t); }

  TypeKind Kind() const override { return TypeKind::kInt32; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) + 1,
                          std::numeric_limits<int32_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeUint32 : public TypeUnsignedInt {
 public:
  TypeUint32() = default;

  size_t Size() const override { return sizeof(uint32_t); }

  TypeKind Kind() const override { return TypeKind::kUint32; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(0, std::numeric_limits<uint32_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInt64 : public TypeSignedInt {
 public:
  TypeInt64() = default;

  size_t Size() const override { return sizeof(int64_t); }

  TypeKind Kind() const override { return TypeKind::kInt64; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1,
                          std::numeric_limits<int64_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeUint64 : public TypeUnsignedInt {
 public:
  TypeUint64() = default;

  size_t Size() const override { return sizeof(uint64_t); }

  TypeKind Kind() const override { return TypeKind::kUint64; }

  std::pair<uint64_t, uint64_t> Limits() const override {
    return std::make_pair(0, std::numeric_limits<uint64_t>::max());
  }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInteger : public Type {
 public:
  TypeInteger() = default;

  // TODO(vbelliard): Use the right size when it will be implemented.
  size_t Size() const override { return 0; }

  TypeKind Kind() const override { return TypeKind::kInteger; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeFloat32 : public TypeRaw {
 public:
  TypeFloat32() = default;

  size_t Size() const override { return sizeof(float); }

  TypeKind Kind() const override { return TypeKind::kFloat32; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeFloat64 : public TypeRaw {
 public:
  TypeFloat64() = default;

  size_t Size() const override { return sizeof(double); }

  TypeKind Kind() const override { return TypeKind::kFloat64; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeObject : public Type {
 public:
  TypeObject(const ObjectSchema* schema) : schema_(schema) {}
  TypeObject() = delete;
  TypeObject(TypeObject&) = delete;
  TypeObject operator=(TypeObject& t) = delete;

  void Dump(std::ostream& os) const override;

  // The size for the type in bytes.
  virtual size_t Size() const override;

  TypeKind Kind() const override { return TypeKind::kObject; }

  // Creates an exact copy of the type.
  virtual std::unique_ptr<Type> Duplicate() const override;

  virtual TypeObject* AsTypeObject() override { return this; }

 private:
  const ObjectSchema* schema_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
