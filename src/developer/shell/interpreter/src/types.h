// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_

#include "src/developer/shell/interpreter/src/nodes.h"

namespace shell {
namespace interpreter {

class TypeBuiltin : public Type {
 public:
  TypeBuiltin() = default;
};

class TypeBool : public TypeBuiltin {
 public:
  TypeBool() = default;

  void Dump(std::ostream& os) const override;
};

class TypeChar : public TypeBuiltin {
 public:
  TypeChar() = default;

  void Dump(std::ostream& os) const override;
};

class TypeString : public TypeBuiltin {
 public:
  TypeString() = default;

  void Dump(std::ostream& os) const override;
};

class TypeInt8 : public TypeBuiltin {
 public:
  TypeInt8() = default;

  void Dump(std::ostream& os) const override;
};

class TypeUint8 : public TypeBuiltin {
 public:
  TypeUint8() = default;

  void Dump(std::ostream& os) const override;
};

class TypeInt16 : public TypeBuiltin {
 public:
  TypeInt16() = default;

  void Dump(std::ostream& os) const override;
};

class TypeUint16 : public TypeBuiltin {
 public:
  TypeUint16() = default;

  void Dump(std::ostream& os) const override;
};

class TypeInt32 : public TypeBuiltin {
 public:
  TypeInt32() = default;

  void Dump(std::ostream& os) const override;
};

class TypeUint32 : public TypeBuiltin {
 public:
  TypeUint32() = default;

  void Dump(std::ostream& os) const override;
};

class TypeInt64 : public TypeBuiltin {
 public:
  TypeInt64() = default;

  void Dump(std::ostream& os) const override;
};

class TypeUint64 : public TypeBuiltin {
 public:
  TypeUint64() = default;

  void Dump(std::ostream& os) const override;
};

class TypeInteger : public TypeBuiltin {
 public:
  TypeInteger() = default;

  void Dump(std::ostream& os) const override;
};

class TypeFloat32 : public TypeBuiltin {
 public:
  TypeFloat32() = default;

  void Dump(std::ostream& os) const override;
};

class TypeFloat64 : public TypeBuiltin {
 public:
  TypeFloat64() = default;

  void Dump(std::ostream& os) const override;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_TYPES_H_
