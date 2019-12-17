// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_TYPE_VISITOR_H_
#define SRC_LIB_FIDL_CODEC_TYPE_VISITOR_H_

#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

// Superclass for implementing visitors for Type. Note that the whole class is protected. To use a
// visitor, use the Visit method on the Type object you want to visit.
class TypeVisitor {
 protected:
  virtual void VisitType(const Type* type) {}
  virtual void VisitRawType(const RawType* type) { VisitType(type); }
  virtual void VisitStringType(const StringType* type) { VisitType(type); }
  virtual void VisitBoolType(const BoolType* type) { VisitType(type); }
  virtual void VisitStructType(const StructType* type) { VisitType(type); }
  virtual void VisitTableType(const TableType* type) { VisitType(type); }
  virtual void VisitUnionType(const UnionType* type) { VisitType(type); }
  virtual void VisitXUnionType(const XUnionType* type) { VisitType(type); }
  virtual void VisitElementSequenceType(const ElementSequenceType* type) { VisitType(type); }
  virtual void VisitArrayType(const ArrayType* type) { VisitElementSequenceType(type); }
  virtual void VisitVectorType(const VectorType* type) { VisitElementSequenceType(type); }
  virtual void VisitEnumType(const EnumType* type) { VisitType(type); }
  virtual void VisitBitsType(const BitsType* type) { VisitType(type); }
  virtual void VisitHandleType(const HandleType* type) { VisitType(type); }
  virtual void VisitNumericType(const Type* type) { VisitType(type); }
  virtual void VisitIntegralType(const Type* type) { VisitNumericType(type); }
  virtual void VisitUint8Type(const Uint8Type* type) { VisitIntegralType(type); }
  virtual void VisitUint16Type(const Uint16Type* type) { VisitIntegralType(type); }
  virtual void VisitUint32Type(const Uint32Type* type) { VisitIntegralType(type); }
  virtual void VisitUint64Type(const Uint64Type* type) { VisitIntegralType(type); }
  virtual void VisitInt8Type(const Int8Type* type) { VisitIntegralType(type); }
  virtual void VisitInt16Type(const Int16Type* type) { VisitIntegralType(type); }
  virtual void VisitInt32Type(const Int32Type* type) { VisitIntegralType(type); }
  virtual void VisitInt64Type(const Int64Type* type) { VisitIntegralType(type); }
  virtual void VisitFloat32Type(const Float32Type* type) { VisitNumericType(type); }
  virtual void VisitFloat64Type(const Float64Type* type) { VisitNumericType(type); }

  friend class Type;
  friend class RawType;
  friend class StringType;
  friend class BoolType;
  friend class StructType;
  friend class TableType;
  friend class UnionType;
  friend class XUnionType;
  friend class ElementSequenceType;
  friend class ArrayType;
  friend class VectorType;
  friend class EnumType;
  friend class BitsType;
  friend class HandleType;
  template <typename T>
  friend class IntegralType;
  template <typename T>
  friend class NumericType;
  friend class Uint8Type;
  friend class Uint16Type;
  friend class Uint32Type;
  friend class Uint64Type;
  friend class Int8Type;
  friend class Int16Type;
  friend class Int32Type;
  friend class Int64Type;
  friend class Float32Type;
  friend class Float64Type;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_TYPE_VISITOR_H_
