// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_

#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

class TypeUndefined : public Type {
 public:
  TypeUndefined() = default;

  size_t Size() const override { return 0; }

  bool IsUndefined() const override { return true; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeBuiltin : public Type {
 public:
  TypeBuiltin() = default;

  Variable* CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                           const std::string& name) const override;
};

class TypeBool : public TypeBuiltin {
 public:
  TypeBool() = default;

  size_t Size() const override { return sizeof(bool); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeChar : public TypeBuiltin {
 public:
  TypeChar() = default;

  size_t Size() const override { return sizeof(uint32_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeString : public Type {
 public:
  TypeString() = default;

  // TODO(vbelliard): Use the right size when it will be implemented.
  size_t Size() const override { return 0; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeInt8 : public TypeBuiltin {
 public:
  TypeInt8() = default;

  size_t Size() const override { return sizeof(int8_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeUint8 : public TypeBuiltin {
 public:
  TypeUint8() = default;

  size_t Size() const override { return sizeof(uint8_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeInt16 : public TypeBuiltin {
 public:
  TypeInt16() = default;

  size_t Size() const override { return sizeof(int16_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeUint16 : public TypeBuiltin {
 public:
  TypeUint16() = default;

  size_t Size() const override { return sizeof(uint16_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeInt32 : public TypeBuiltin {
 public:
  TypeInt32() = default;

  size_t Size() const override { return sizeof(int32_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeUint32 : public TypeBuiltin {
 public:
  TypeUint32() = default;

  size_t Size() const override { return sizeof(uint32_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeInt64 : public TypeBuiltin {
 public:
  TypeInt64() = default;

  size_t Size() const override { return sizeof(int64_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeUint64 : public TypeBuiltin {
 public:
  TypeUint64() = default;

  size_t Size() const override { return sizeof(uint64_t); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;

  void GenerateDefaultValue(ExecutionContext* context, code::Code* code) const override;

  void GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                              const IntegerLiteral* literal) const override;

  void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const override;
};

class TypeInteger : public Type {
 public:
  TypeInteger() = default;

  // TODO(vbelliard): Use the right size when it will be implemented.
  size_t Size() const override { return 0; }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeFloat32 : public TypeBuiltin {
 public:
  TypeFloat32() = default;

  size_t Size() const override { return sizeof(float); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

class TypeFloat64 : public TypeBuiltin {
 public:
  TypeFloat64() = default;

  size_t Size() const override { return sizeof(double); }

  std::unique_ptr<Type> Duplicate() const override;

  void Dump(std::ostream& os) const override;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
