// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/cast.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"

namespace zxdb {

namespace {

// Returns true if this type is enough like an integer to support conversion
// to another number type. This includes all base types except floating point.
bool IsIntegerLike(const Type* t) {
  // Pointers count.
  if (const ModifiedType* modified_type = t->AsModifiedType())
    return modified_type->tag() == DwarfTag::kPointerType;

  const BaseType* base_type = t->AsBaseType();
  if (!base_type)
    return false;

  int kind = base_type->base_type();
  return kind == BaseType::kBaseTypeAddress ||
         kind == BaseType::kBaseTypeBoolean ||
         kind == BaseType::kBaseTypeSigned ||
         kind == BaseType::kBaseTypeSignedChar ||
         kind == BaseType::kBaseTypeUnsigned ||
         kind == BaseType::kBaseTypeUnsignedChar ||
         kind == BaseType::kBaseTypeUTF;
}

bool IsSignedBaseType(const Type* type) {
  const BaseType* base_type = type->AsBaseType();
  if (!base_type)
    return false;
  int kind = base_type->base_type();
  return kind == BaseType::kBaseTypeSigned ||
         kind == BaseType::kBaseTypeSignedChar;
}

bool IsBooleanBaseType(const Type* type) {
  const BaseType* base_type = type->AsBaseType();
  if (!base_type)
    return false;
  return base_type->base_type() == BaseType::kBaseTypeBoolean;
}

bool IsFloatingPointBaseType(const Type* type) {
  const BaseType* base_type = type->AsBaseType();
  if (!base_type)
    return false;
  return base_type->base_type() == BaseType::kBaseTypeFloat;
}

// Numbers include integers and floating point.
bool IsNumberLike(const Type* t) {
  return IsIntegerLike(t) || IsFloatingPointBaseType(t);
}

// Creates an ExprValue with the contents of the given "value". The size of
// "value" must match the destination type. This function always places the
// output into *result and returns an empty Err() for the convenience of the
// callers.
template <typename T>
Err CreateValue(T value, const fxl::RefPtr<Type>& dest_type,
                const ExprValueSource& dest_source, ExprValue* result) {
  FXL_DCHECK(sizeof(T) == dest_type->byte_size());

  std::vector<uint8_t> dest_bytes;
  dest_bytes.resize(sizeof(T));
  memcpy(&dest_bytes[0], &value, sizeof(T));

  *result = ExprValue(dest_type, std::move(dest_bytes), dest_source);
  return Err();
}

std::vector<uint8_t> CastToIntegerOfSize(const std::vector<uint8_t>& source,
                                         bool source_is_signed,
                                         size_t dest_size) {
  if (source.size() > dest_size) {
    // Truncate. Assume little-endian so copy from the beginning to get the low
    // bits.
    return std::vector<uint8_t>(source.begin(), source.begin() + dest_size);
  } else if (source.size() < dest_size) {
    // Extend.
    std::vector<uint8_t> result = source;
    if (source_is_signed && result.back() & 0b10000000) {
      // Sign-extend.
      result.resize(dest_size, 0xff);
    } else {
      // 0-extend.
      result.resize(dest_size);
    }
    return result;
  }
  return source;  // No change.
}

// The "Int64" parameter is either "uint64_t" or "int64_t" depending on the
// signedness of the integer desired.
template <typename Int64>
ExprValue CastFloatToIntT(double double_value,
                          const fxl::RefPtr<Type>& dest_type,
                          const ExprValueSource& dest_source) {
  Int64 int64_value = static_cast<Int64>(double_value);

  std::vector<uint8_t> int64_data;
  int64_data.resize(sizeof(Int64));
  memcpy(&int64_data[0], &int64_value, sizeof(Int64));

  // CastToIntegerOfSize will downcast the int64 to the desired result size.
  return ExprValue(
      dest_type, CastToIntegerOfSize(int64_data, true, dest_type->byte_size()),
      dest_source);
}

Err CastFloatToInt(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                   const Type* concrete_dest_type,
                   const ExprValueSource& dest_source, ExprValue* result) {
  double source_value;
  Err err = source.PromoteToDouble(&source_value);
  if (err.has_error())
    return err;

  if (IsSignedBaseType(concrete_dest_type)) {
    *result = CastFloatToIntT<int64_t>(source_value, dest_type, dest_source);
    return Err();
  } else {
    *result = CastFloatToIntT<uint64_t>(source_value, dest_type, dest_source);
    return Err();
  }
  return Err("Can't convert a floating-point of size %u to an integer.",
             source.type()->byte_size());
}

// Converts an integer value into to a binary representation of a float/double.
// The "Int" template type should be a [u]int64_t of the signedness of the
// source type, and the "Float" type is the output type required.
template <typename Int, typename Float>
Err CastIntToFloatT(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                    const ExprValueSource& dest_source, ExprValue* result) {
  // Get the integer out as a 64-bit value of the correct sign.
  Int source_int;
  Err err = source.PromoteTo64(&source_int);
  if (err.has_error())
    return err;

  return CreateValue(static_cast<Float>(source_int), dest_type, dest_source,
                     result);
}

Err CastIntToFloat(const ExprValue& source, bool source_is_signed,
                   const fxl::RefPtr<Type>& dest_type,
                   const ExprValueSource& dest_source, ExprValue* result) {
  if (source_is_signed) {
    if (dest_type->byte_size() == 4) {
      return CastIntToFloatT<int64_t, float>(source, dest_type, dest_source,
                                             result);
    } else if (dest_type->byte_size() == 8) {
      return CastIntToFloatT<int64_t, double>(source, dest_type, dest_source,
                                              result);
    }
  } else {
    if (dest_type->byte_size() == 4) {
      return CastIntToFloatT<uint64_t, float>(source, dest_type, dest_source,
                                              result);
    } else if (dest_type->byte_size() == 8) {
      return CastIntToFloatT<uint64_t, double>(source, dest_type, dest_source,
                                               result);
    }
  }

  return Err("Can't convert to floating-point number of size %u.",
             dest_type->byte_size());
}

Err CastFloatToFloat(const ExprValue& source,
                     const fxl::RefPtr<Type>& dest_type,
                     const ExprValueSource& dest_source, ExprValue* result) {
  if (source.data().size() == 4) {
    float f = source.GetAs<float>();
    if (dest_type->byte_size() == 4)
      return CreateValue<float>(f, dest_type, dest_source, result);
    else if (dest_type->byte_size() == 8)
      return CreateValue<double>(f, dest_type, dest_source, result);
  } else if (source.data().size() == 8) {
    double d = source.GetAs<double>();
    if (dest_type->byte_size() == 4)
      return CreateValue<float>(d, dest_type, dest_source, result);
    else if (dest_type->byte_size() == 8)
      return CreateValue<double>(d, dest_type, dest_source, result);
  }

  return Err("Can't convert floating-point from size %zu to %u.",
             source.data().size(), dest_type->byte_size());
}

Err CastNumberToBool(const ExprValue& source, const Type* concrete_from,
                     const fxl::RefPtr<Type>& dest_type,
                     const ExprValueSource& dest_source, ExprValue* result) {
  bool value = false;

  if (IsIntegerLike(concrete_from)) {
    // All integer-like sources just look for non-zero bytes.
    for (uint8_t cur : source.data()) {
      if (cur) {
        value = true;
        break;
      }
    }
  } else {
    // floating-point-like sources which can't do a byte-by-byte comparison.
    FXL_DCHECK(IsFloatingPointBaseType(concrete_from));
    double double_value;
    Err err = source.PromoteToDouble(&double_value);
    if (err.has_error())
      return err;

    // Use C++ casting rules to convert to bool.
    value = !!double_value;
  }

  // The data buffer that will be returned, matching the size of the boolean.
  std::vector<uint8_t> dest_data;
  dest_data.resize(dest_type->byte_size());
  if (value)
    dest_data[0] = 1;

  *result = ExprValue(dest_type, std::move(dest_data), dest_source);
  return Err();
}

// Returns true if the two concrete types (as a result of calling
// Type::GetConcreteType()) can be coerced by copying the data. This includes
// things that are actually the same, as well as things like signed/unsigned
// conversions and pointer/int conversions that our very loose coercion rules
// support.
bool TypesAreBinaryCoercable(const Type* a, const Type* b) {
  // TODO(brettw) need to handle bit fields.
  if (a->byte_size() != b->byte_size())
    return false;  // Sizes must match or copying definitely won't work.

  // It's possible for things to have the same type but different Type objects
  // depending on how the types were arrived at and whether the source and dest
  // are from the same compilation unit. Assume if the string names of the
  // types match as well as the size, it's the same type.
  if (a->GetFullName() == b->GetFullName())
    return true;  // Names match, assume same type.

  // Allow all integers and pointers of the same size to be converted by
  // copying.
  return IsIntegerLike(a) && IsIntegerLike(b);
}

Err ImplicitCast(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                 const ExprValueSource& dest_source, ExprValue* result) {
  // There are several fundamental types of things that can be casted:
  //  - Aggregate types: Can only convert if they're the same.
  //  - Integers and integer-like things: This includes pointers.
  //  - Floating-point numbers.
  //  - Booleans.

  // Prevent crashes if we get bad types with no size.
  if (source.data().size() == 0 || dest_type->byte_size() == 0)
    return Err("Type has 0 size.");

  // Get the types without "const", etc. modifiers.
  const Type* concrete_from = source.type()->GetConcreteType();
  const Type* concrete_to = dest_type->GetConcreteType();

  // Handles identical type conversions. This includes all aggregate types.
  if (TypesAreBinaryCoercable(concrete_from, concrete_to)) {
    *result = ExprValue(dest_type, source.data(), dest_source);
    return Err();
  }

  // Conversions to bool. Conversions from bool will follow the standard
  // "number to X" path where we assume the bool is like a number.
  if (IsBooleanBaseType(concrete_to) && IsNumberLike(concrete_from)) {
    return CastNumberToBool(source, concrete_from, dest_type, dest_source,
                            result);
  }

  // Conversions between different types of ints (truncate or extend).
  if (IsIntegerLike(concrete_from) && IsIntegerLike(concrete_to)) {
    *result = ExprValue(
        dest_type,
        CastToIntegerOfSize(source.data(), IsSignedBaseType(concrete_from),
                            concrete_to->byte_size()),
        dest_source);
    return Err();
  }

  // Conversions between different types of floats.
  if (IsFloatingPointBaseType(concrete_from) &&
      IsFloatingPointBaseType(concrete_to))
    return CastFloatToFloat(source, dest_type, dest_source, result);

  // Conversions between ints and floats.
  if (IsIntegerLike(concrete_to) && IsFloatingPointBaseType(concrete_from))
    return CastFloatToInt(source, dest_type, concrete_to, dest_source, result);
  if (IsFloatingPointBaseType(concrete_to) && IsIntegerLike(concrete_from)) {
    return CastIntToFloat(source, IsSignedBaseType(concrete_from), dest_type,
                          dest_source, result);
  }

  return Err("Can't cast from '%s' to '%s'.",
             source.type()->GetFullName().c_str(),
             dest_type->GetFullName().c_str());
}

Err ReinterpretCast(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                    const ExprValueSource& dest_source, ExprValue* result) {
  if (!source.type())
    return Err("Can't cast from a null type.");
  if (!dest_type)
    return Err("Can't cast to a null type.");

  // The input and output types should both be integer-like (this includes
  // pointers). This check is more restrictive than the "coerce" rules above
  // because we don't want to support things like integer-to-double conversion.
  const Type* concrete_source = source.type()->GetConcreteType();
  if (!IsIntegerLike(concrete_source)) {
    return Err("Can't cast from a '%s'.", source.type()->GetFullName().c_str());
  }

  const Type* concrete_dest = dest_type->GetConcreteType();
  if (!IsIntegerLike(concrete_dest))
    return Err("Can't cast to a '%s'.", dest_type->GetFullName().c_str());

  // Our implementation of reinterpret_cast is just a bit cast with truncation
  // or 0-fill (not sign extend). C++ would require the type sizes match and
  // would prohibit most number-to-number conversions, but those restrictions
  // aren't useful or even desirable in the case of a debugger handling user
  // input.
  auto new_data = source.data();
  new_data.resize(dest_type->byte_size());
  *result = ExprValue(dest_type, std::move(new_data), dest_source);
  return Err();
}

}  // namespace

const char* CastTypeToString(CastType type) {
  switch (type) {
    case CastType::kImplicit:
      return "implicit";
    case CastType::kC:
      return "C";
    case CastType::kReinterpret:
      return "reinterpret_cast";
  }
  return "<invalid>";
}

Err CastExprValue(CastType cast_type, const ExprValue& source,
                  const fxl::RefPtr<Type>& dest_type, ExprValue* result,
                  const ExprValueSource& dest_source) {
  switch (cast_type) {
    case CastType::kImplicit:
      return ImplicitCast(source, dest_type, dest_source, result);
    case CastType::kC: {
      // A C-style cast can do the following things.
      //  - const_cast
      //  - static_cast
      //  - static_cast followed by a const_cast
      //  - reinterpret_cast
      //  - reinterpret_cast followed by a const_cast
      //
      // Since the debugger ignores const in debugging, this ends up being
      // a static cast falling back to a reinterpret cast.
      //
      // TODO(DX-1178) this should be a static cast when it exists. Currently
      // "coerce" implements the things we're willing to implicitly cast
      // and doesn't handle things like derived type conversions.
      if (!ImplicitCast(source, dest_type, dest_source, result).has_error())
        return Err();
      return ReinterpretCast(source, dest_type, dest_source, result);
    }
    case CastType::kReinterpret:
      return ReinterpretCast(source, dest_type, dest_source, result);
  }
  FXL_NOTREACHED();
  return Err("Internal error.");
}

}  // namespace zxdb
