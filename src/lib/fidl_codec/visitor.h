// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_VISITOR_H_
#define SRC_LIB_FIDL_CODEC_VISITOR_H_

#include "src/lib/fidl_codec/wire_object.h"

namespace fidl_codec {

// Superclass for implementing visitors for Fields. Note that the whole class is protected. To use a
// visitor, use the Visit method on the Field object you want to visit.
class Visitor {
 protected:
  virtual void VisitField(const Field* node) {}
  virtual void VisitNullableField(const NullableField* node) { VisitField(node); }
  virtual void VisitInlineField(const InlineField* node) { VisitField(node); }
  virtual void VisitRawField(const RawField* node) { VisitField(node); }
  virtual void VisitObject(const Object* node) { VisitNullableField(node); }
  virtual void VisitStringField(const StringField* node) { VisitNullableField(node); }
  virtual void VisitBoolField(const BoolField* node) { VisitInlineField(node); }
  virtual void VisitEnvelopeField(const EnvelopeField* node) { VisitNullableField(node); }
  virtual void VisitTableField(const TableField* node) { VisitNullableField(node); }
  virtual void VisitUnionField(const UnionField* node) { VisitNullableField(node); }
  virtual void VisitXUnionField(const XUnionField* node) { VisitUnionField(node); }
  virtual void VisitArrayField(const ArrayField* node) { VisitField(node); }
  virtual void VisitVectorField(const VectorField* node) { VisitNullableField(node); }
  virtual void VisitEnumField(const EnumField* node) { VisitInlineField(node); }
  virtual void VisitHandleField(const HandleField* node) { VisitField(node); }

  virtual void VisitNumericField(const InlineField* node) { VisitInlineField(node); }
  virtual void VisitU8Field(const NumericField<uint8_t>* node) { VisitNumericField(node); }
  virtual void VisitU16Field(const NumericField<uint16_t>* node) { VisitNumericField(node); }
  virtual void VisitU32Field(const NumericField<uint32_t>* node) { VisitNumericField(node); }
  virtual void VisitU64Field(const NumericField<uint64_t>* node) { VisitNumericField(node); }
  virtual void VisitI8Field(const NumericField<int8_t>* node) { VisitNumericField(node); }
  virtual void VisitI16Field(const NumericField<int16_t>* node) { VisitNumericField(node); }
  virtual void VisitI32Field(const NumericField<int32_t>* node) { VisitNumericField(node); }
  virtual void VisitI64Field(const NumericField<int64_t>* node) { VisitNumericField(node); }
  virtual void VisitF32Field(const NumericField<float>* node) { VisitNumericField(node); }
  virtual void VisitF64Field(const NumericField<double>* node) { VisitNumericField(node); }

  friend class Field;
  friend class NullableField;
  friend class InlineField;
  friend class RawField;
  template <typename T>
  friend class NumericField;
  friend class StringField;
  friend class BoolField;
  friend class Object;
  friend class EnvelopeField;
  friend class TableField;
  friend class UnionField;
  friend class XUnionField;
  friend class ArrayField;
  friend class VectorField;
  friend class EnumField;
  friend class HandleField;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_VISITOR_H_
