// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/cast.h"

#include <optional>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

namespace zxdb {

namespace {

// Returns true if this type is enough like an integer to support conversion to another number type.
// This includes all base types except floating point.
bool IsIntegerLike(const Type* t) {
  // Pointers count.
  if (const ModifiedType* modified_type = t->AsModifiedType())
    return modified_type->tag() == DwarfTag::kPointerType;

  // Enums count.
  if (t->AsEnumeration())
    return true;

  const BaseType* base_type = t->AsBaseType();
  if (!base_type)
    return false;

  int kind = base_type->base_type();
  return kind == BaseType::kBaseTypeAddress || kind == BaseType::kBaseTypeBoolean ||
         kind == BaseType::kBaseTypeSigned || kind == BaseType::kBaseTypeSignedChar ||
         kind == BaseType::kBaseTypeUnsigned || kind == BaseType::kBaseTypeUnsignedChar ||
         kind == BaseType::kBaseTypeUTF;
}

bool IsSignedBaseType(const Type* type) {
  const BaseType* base_type = type->AsBaseType();
  if (!base_type)
    return false;
  return BaseType::IsSigned(base_type->base_type());
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
bool IsNumberLike(const Type* t) { return IsIntegerLike(t) || IsFloatingPointBaseType(t); }

std::vector<uint8_t> CastToIntegerOfSize(const std::vector<uint8_t>& source, bool source_is_signed,
                                         size_t dest_size) {
  if (source.size() > dest_size) {
    // Truncate. Assume little-endian so copy from the beginning to get the low bits.
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

ExprValue CastIntToInt(const ExprValue& source, const Type* source_type,
                       const fxl::RefPtr<Type>& dest_type, const ExprValueSource& dest_source) {
  return ExprValue(
      dest_type,
      CastToIntegerOfSize(source.data(), IsSignedBaseType(source_type), dest_type->byte_size()),
      dest_source);
}

// The "Int64" parameter is either "uint64_t" or "int64_t" depending on the signedness of the
// integer desired.
template <typename Int64>
ExprValue CastFloatToIntT(double double_value, const fxl::RefPtr<Type>& dest_type,
                          const ExprValueSource& dest_source) {
  Int64 int64_value = static_cast<Int64>(double_value);

  std::vector<uint8_t> int64_data;
  int64_data.resize(sizeof(Int64));
  memcpy(&int64_data[0], &int64_value, sizeof(Int64));

  // CastToIntegerOfSize will downcast the int64 to the desired result size.
  return ExprValue(dest_type, CastToIntegerOfSize(int64_data, true, dest_type->byte_size()),
                   dest_source);
}

ErrOrValue CastFloatToInt(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                          const Type* concrete_dest_type, const ExprValueSource& dest_source) {
  double source_value;
  if (Err err = source.PromoteToDouble(&source_value); err.has_error())
    return err;

  if (IsSignedBaseType(concrete_dest_type))
    return CastFloatToIntT<int64_t>(source_value, dest_type, dest_source);
  else
    return CastFloatToIntT<uint64_t>(source_value, dest_type, dest_source);

  return Err("Can't convert a floating-point of size %u to an integer.",
             source.type()->byte_size());
}

// Converts an integer value into to a binary representation of a float/double. The "Int" template
// type should be a [u]int64_t of the signedness of the source type, and the "Float" type is the
// output type required.
template <typename Int, typename Float>
ErrOrValue CastIntToFloatT(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                           const ExprValueSource& dest_source) {
  // Get the integer out as a 64-bit value of the correct sign.
  Int source_int;
  if (Err err = source.PromoteTo64(&source_int); err.has_error())
    return err;

  return ExprValue(static_cast<Float>(source_int), dest_type, dest_source);
}

ErrOrValue CastIntToFloat(const ExprValue& source, bool source_is_signed,
                          const fxl::RefPtr<Type>& dest_type, const ExprValueSource& dest_source) {
  if (source_is_signed) {
    if (dest_type->byte_size() == 4)
      return CastIntToFloatT<int64_t, float>(source, dest_type, dest_source);
    else if (dest_type->byte_size() == 8)
      return CastIntToFloatT<int64_t, double>(source, dest_type, dest_source);
  } else {
    if (dest_type->byte_size() == 4)
      return CastIntToFloatT<uint64_t, float>(source, dest_type, dest_source);
    else if (dest_type->byte_size() == 8)
      return CastIntToFloatT<uint64_t, double>(source, dest_type, dest_source);
  }

  return Err("Can't convert to floating-point number of size %u.", dest_type->byte_size());
}

ErrOrValue CastFloatToFloat(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                            const ExprValueSource& dest_source) {
  if (source.data().size() == 4) {
    float f = source.GetAs<float>();
    if (dest_type->byte_size() == 4)
      return ExprValue(f, dest_type, dest_source);
    else if (dest_type->byte_size() == 8)
      return ExprValue(static_cast<double>(f), dest_type, dest_source);
  } else if (source.data().size() == 8) {
    double d = source.GetAs<double>();
    if (dest_type->byte_size() == 4)
      return ExprValue(static_cast<float>(d), dest_type, dest_source);
    else if (dest_type->byte_size() == 8)
      return ExprValue(d, dest_type, dest_source);
  }

  return Err("Can't convert floating-point from size %zu to %u.", source.data().size(),
             dest_type->byte_size());
}

ErrOrValue CastNumberToBool(const ExprValue& source, const Type* concrete_from,
                            const fxl::RefPtr<Type>& dest_type,
                            const ExprValueSource& dest_source) {
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
    // Floating-point-like sources which can't do a byte-by-byte comparison.
    FXL_DCHECK(IsFloatingPointBaseType(concrete_from));
    double double_value;
    if (Err err = source.PromoteToDouble(&double_value); err.has_error())
      return err;

    // Use C++ casting rules to convert to bool.
    value = !!double_value;
  }

  // The data buffer that will be returned, matching the size of the boolean.
  std::vector<uint8_t> dest_data;
  dest_data.resize(dest_type->byte_size());
  if (value)
    dest_data[0] = 1;

  return ExprValue(dest_type, std::move(dest_data), dest_source);
}

// Returns true if the two concrete types (resulting from previously calling
// EvalContext::GetConcreteType()) can be coerced by copying the data. This includes things that are
// actually the same, as well as things like signed/unsigned conversions and pointer/int conversions
// that our very loose coercion rules support.
bool TypesAreBinaryCoercable(const Type* a, const Type* b) {
  // TODO(brettw) need to handle bit fields.
  if (a->byte_size() != b->byte_size())
    return false;  // Sizes must match or copying definitely won't work.

  // It's possible for things to have the same type but different Type objects depending on how the
  // types were arrived at and whether the source and dest are from the same compilation unit.
  // Assume if the string names of the types match as well as the size, it's the same type.
  if (a->GetFullName() == b->GetFullName())
    return true;  // Names match, assume same type.

  // Allow integers and pointers of the same size to be converted by copying.
  if (a->tag() == DwarfTag::kPointerType && b->tag() == DwarfTag::kPointerType) {
    // Don't allow pointer-to-pointer conversions because those might need to
    // be adjusted according to base/derived classes.
    return false;
  }
  return IsIntegerLike(a) && IsIntegerLike(b);
}

// Checks whether the two input types have the specified base/derived relationship (this does not
// check for a relationship going in the opposite direction). If so, returns the offset of the base
// class in the derived class. If not, returns an empty optional.
//
// The two types must have c-v qualifiers stripped.
std::optional<uint64_t> GetDerivedClassOffset(const Type* base, const Type* derived) {
  const Collection* derived_collection = derived->AsCollection();
  if (!derived_collection)
    return std::nullopt;

  const Collection* base_collection = base->AsCollection();
  if (!base_collection)
    return std::nullopt;
  std::string base_name = base_collection->GetFullName();

  std::optional<uint64_t> result;
  VisitClassHierarchy(derived_collection,
                      [&result, &base_name](const Collection* cur, uint64_t offset) {
                        if (cur->GetFullName() == base_name) {
                          result = offset;
                          return VisitResult::kDone;
                        }
                        return VisitResult::kContinue;
                      });
  return result;
}

Err MakeCastError(const Type* from, const Type* to) {
  return Err("Can't cast '%s' to '%s'.", from->GetFullName().c_str(), to->GetFullName().c_str());
}

// Flag that indicates whether a base class' pointer or reference can be converted to a derived
// class' pointer or reference. Implicit casts don't do this, but if the user explicitly asks (e.g.
// "static_cast<Derived>") we allow it.
enum CastPointer { kAllowBaseToDerived, kDisallowBaseToDerived };

// Converts a pointer/reference to a pointer/reference to a different type according to approximate
// static_cast rules.
//
// The source and dest types should already be concrete (from EvalContext::GetConcreteType()).
void StaticCastPointerOrRef(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& source,
                            const fxl::RefPtr<Type>& dest_type, const Type* concrete_from,
                            const Type* concrete_to, const ExprValueSource& dest_source,
                            CastPointer cast_pointer, EvalCallback cb) {
  if (!DwarfTagIsPointerOrReference(concrete_from->tag()) ||
      !DwarfTagIsPointerOrReference(concrete_to->tag()))
    return cb(MakeCastError(concrete_from, concrete_to));

  // The pointer/ref-ness must match from the source to the dest. This code treats rvalue references
  // and regular references the same.
  if ((concrete_from->tag() == DwarfTag::kPointerType) !=
          (concrete_to->tag() == DwarfTag::kPointerType) ||
      DwarfTagIsEitherReference(concrete_from->tag()) !=
          DwarfTagIsEitherReference(concrete_to->tag())) {
    return cb(MakeCastError(concrete_from, concrete_to));
  }

  // Can assume they're ModifiedTypes due to tag checks above.
  const ModifiedType* modified_from = concrete_from->AsModifiedType();
  const ModifiedType* modified_to = concrete_to->AsModifiedType();
  if (modified_from->ModifiesVoid() || modified_to->ModifiesVoid()) {
    // Always allow conversions to and from void*. This technically handles void& which isn't
    // expressible C++, but should be fine.
    return cb(CastIntToInt(source, concrete_from, dest_type, dest_source));
  }

  // Currently we assume all pointers and references are 64-bit.
  if (modified_from->byte_size() != sizeof(uint64_t) ||
      modified_to->byte_size() != sizeof(uint64_t)) {
    return cb(
        Err("Can only cast 64-bit pointers and references: "
            "'%s' is %u bytes and '%s' is %u bytes.",
            concrete_from->GetFullName().c_str(), concrete_from->byte_size(),
            concrete_to->GetFullName().c_str(), concrete_to->byte_size()));
  }

  // Get the pointed-to or referenced types.
  const Type* refed_from_abstract = modified_from->modified().Get()->AsType();
  const Type* refed_to_abstract = modified_to->modified().Get()->AsType();
  if (!refed_from_abstract || !refed_to_abstract) {
    // Error decoding (not void* because that was already checked above).
    return cb(MakeCastError(concrete_from, concrete_to));
  }

  // Strip qualifiers to handle things like "pointer to const int".
  fxl::RefPtr<Type> refed_from = eval_context->GetConcreteType(refed_from_abstract);
  fxl::RefPtr<Type> refed_to = eval_context->GetConcreteType(refed_to_abstract);

  if (refed_from->GetFullName() == refed_to->GetFullName()) {
    // Source and dest are the same type.
    return cb(CastIntToInt(source, concrete_from, dest_type, dest_source));
  }

  if (auto found_offset = GetDerivedClassOffset(refed_to.get(), refed_from.get())) {
    // Convert derived class ref/ptr to base class ref/ptr. This requires adjusting the pointer to
    // point to where the base class is inside of the derived class.

    // The 64-bit-edness of both pointers was checked above.
    uint64_t ptr_value = source.GetAs<uint64_t>();
    ptr_value += *found_offset;

    return cb(ExprValue(ptr_value, dest_type, dest_source));
  }

  if (cast_pointer == kAllowBaseToDerived) {
    // The reverse of the above case. This is used when the user knows a base class
    // pointer/reference actually points to a specific derived class.
    if (auto found_offset = GetDerivedClassOffset(refed_from.get(), refed_to.get())) {
      uint64_t ptr_value = source.GetAs<uint64_t>();
      ptr_value -= *found_offset;
      return cb(ExprValue(ptr_value, dest_type, dest_source));
    }
  }

  cb(Err("Can't convert '%s' to unrelated type '%s'.", concrete_from->GetFullName().c_str(),
         concrete_to->GetFullName().c_str()));
}

// Some types of casts requires that references be followed (e.g. int& -> long), while others
// require that they not be followed (e.g. BaseClass& -> DerivedClass&). This function
// determines if the source should have references followed before executing the cast.
bool CastShouldFollowReferences(const fxl::RefPtr<EvalContext>& eval_context, CastType cast_type,
                                const ExprValue& source, const fxl::RefPtr<Type>& dest_type) {
  // Implicit casts never follow references. If you have two references:
  //   A& a;
  //   B& b;
  // and do:
  //   a = b;
  // This ends up being an implicit cast, but should assign the values, not convert references. This
  // is different than an explicit cast:
  //   (B&)a;
  // Which converts the reference itself.
  if (cast_type == CastType::kImplicit)
    return true;

  // Casting a reference to a reference needs to keep the reference information. Casting a reference
  // to anything else means the reference should be stripped.
  fxl::RefPtr<Type> concrete_from = eval_context->GetConcreteType(source.type());
  fxl::RefPtr<Type> concrete_to = eval_context->GetConcreteType(dest_type.get());

  // Count rvalue references as references. This isn't always strictly valid since you can't static
  // cast a Base&& to a Derived&&, but from a debugger perspective there's no reason not to allow
  // this.
  if (DwarfTagIsEitherReference(concrete_from->tag()) &&
      DwarfTagIsEitherReference(concrete_to->tag()))
    return false;  // Keep reference on source for casting.

  return true;  // Follow reference.
}

// Handles the synchronous "number" variants of an implicit cast.
//
// The dest_type is the original type that the output will be, which might be non-concrete (const,
// etc.). The concrete_to/from must be the concrete versions that we'll work off of.
ErrOrValue NumericImplicitCast(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                               const Type* concrete_to, const Type* concrete_from,
                               const ExprValueSource& dest_source) {
  // Handles identical type conversions. This includes all aggregate types.
  if (TypesAreBinaryCoercable(concrete_from, concrete_to))
    return ExprValue(dest_type, source.data(), dest_source);

  // Conversions to bool. Conversions from bool will follow the standard "number to X" path where we
  // assume the bool is like a number.
  if (IsBooleanBaseType(concrete_to) && IsNumberLike(concrete_from))
    return CastNumberToBool(source, concrete_from, dest_type, dest_source);

  // Conversions between different types of ints, including pointers (truncate or extend). This lets
  // us evaluate things like "ptr = 0x2a3512635" without elaborate casts. Pointer-to-pointer
  // conversions need to check for derived classes so can't be handled by this function.
  if (IsIntegerLike(concrete_from) && IsIntegerLike(concrete_to) &&
      !(concrete_from->tag() == DwarfTag::kPointerType &&
        concrete_to->tag() == DwarfTag::kPointerType))
    return CastIntToInt(source, concrete_from, dest_type, dest_source);

  // Conversions between different types of floats.
  if (IsFloatingPointBaseType(concrete_from) && IsFloatingPointBaseType(concrete_to))
    return CastFloatToFloat(source, dest_type, dest_source);

  // Conversions between ints and floats.
  if (IsIntegerLike(concrete_to) && IsFloatingPointBaseType(concrete_from))
    return CastFloatToInt(source, dest_type, concrete_to, dest_source);
  if (IsFloatingPointBaseType(concrete_to) && IsIntegerLike(concrete_from))
    return CastIntToFloat(source, IsSignedBaseType(concrete_from), dest_type, dest_source);

  return Err("Can't cast from '%s' to '%s'.", source.type()->GetFullName().c_str(),
             dest_type->GetFullName().c_str());
}

// Attempts an implicit cast, handling numbers (synchonous) and derived types (possibly asynchronous
// for virtual inheritance).
void ImplicitCast(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& source,
                  const fxl::RefPtr<Type>& dest_type, const ExprValueSource& dest_source,
                  EvalCallback cb) {
  // Prevent crashes if we get bad types with no size.
  if (source.data().size() == 0 || dest_type->byte_size() == 0)
    return cb(Err("Type has 0 size."));

  // Get the types without "const", etc. modifiers.
  fxl::RefPtr<Type> concrete_from = eval_context->GetConcreteType(source.type());
  fxl::RefPtr<Type> concrete_to = eval_context->GetConcreteType(dest_type.get());

  ErrOrValue result =
      NumericImplicitCast(source, dest_type, concrete_to.get(), concrete_from.get(), dest_source);
  if (result.ok())
    return cb(result);

  // Pointer-to-pointer conversions. Allow anything that can be static_cast-ed which is permissive
  // but a little more strict than in other conversions: if you have two unrelated pointers,
  // converting magically between them is error prone. LLDB does this extra checking, while GDB
  // always allows the conversions.
  if (concrete_from->tag() == DwarfTag::kPointerType &&
      concrete_to->tag() == DwarfTag::kPointerType) {
    // Note that implicit cast does not do this for references. If "a" and "b" are both references,
    // we want "a = b" to copy the referenced objects, not the reference pointers. The reference
    // conversion feature of this function is used for static casting where static_cast<A&>(b)
    // refers to the reference address and not the referenced object.
    return StaticCastPointerOrRef(eval_context, source, dest_type, concrete_from.get(),
                                  concrete_to.get(), dest_source, kDisallowBaseToDerived,
                                  std::move(cb));
  }

  // Conversions to base classes (on objects, not on pointers or references). e.g. "foo = bar" where
  // foo's type is a base class of bar's.
  if (auto found_offset = GetDerivedClassOffset(concrete_to.get(), concrete_from.get())) {
    // Ignore the dest_source. ResolveInherited is extracting data from inside the source object
    // which has a well-defined source location (unlike for all other casts that change the data so
    // there isn't so clear a source).
    // TODO(brettw) use the asynchronous version instead.
    return cb(ResolveInherited(eval_context, source, dest_type, *found_offset));
  }

  cb(Err("Can't cast from '%s' to '%s'.", source.type()->GetFullName().c_str(),
         dest_type->GetFullName().c_str()));
}

ErrOrValue ReinterpretCast(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& source,
                           const fxl::RefPtr<Type>& dest_type, const ExprValueSource& dest_source) {
  if (!source.type())
    return Err("Can't cast from a null type.");
  if (!dest_type)
    return Err("Can't cast to a null type.");

  // The input and output types should both be integer-like (this includes pointers). This check is
  // more restrictive than the "coerce" rules above because we don't want to support things like
  // integer-to-double conversion.
  fxl::RefPtr<Type> concrete_source = eval_context->GetConcreteType(source.type());
  if (!IsIntegerLike(concrete_source.get()))
    return Err("Can't cast from a '%s'.", source.type()->GetFullName().c_str());

  fxl::RefPtr<Type> concrete_dest = eval_context->GetConcreteType(dest_type.get());
  if (!IsIntegerLike(concrete_dest.get()))
    return Err("Can't cast to a '%s'.", dest_type->GetFullName().c_str());

  // Our implementation of reinterpret_cast is just a bit cast with truncation or 0-fill (not sign
  // extend). C++ would require the type sizes match and would prohibit most number-to-number
  // conversions, but those restrictions aren't useful or even desirable in the case of a debugger
  // handling user input.
  auto new_data = source.data();
  new_data.resize(dest_type->byte_size());
  return ExprValue(dest_type, std::move(new_data), dest_source);
}

void StaticCast(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& source,
                const fxl::RefPtr<Type>& dest_type, const ExprValueSource& dest_source,
                EvalCallback cb) {
  // Our implicit cast is permissive enough to handle most cases including all number conversions,
  // and casts to base types.
  ImplicitCast(eval_context, source, dest_type, dest_source,
               [eval_context, source, dest_type, dest_source,
                cb = std::move(cb)](ErrOrValue result) mutable {
                 if (result.ok())
                   return cb(result);

                 // On failure, fall back on extra things allowed by static_cast.

                 // Get the types without "const", etc. modifiers.
                 fxl::RefPtr<Type> concrete_from = eval_context->GetConcreteType(source.type());
                 fxl::RefPtr<Type> concrete_to = eval_context->GetConcreteType(dest_type.get());

                 // Static casts explicitly allow conversion of pointers to a derived class my
                 // modifying the address being pointed to.
                 StaticCastPointerOrRef(eval_context, source, dest_type, concrete_from.get(),
                                        concrete_to.get(), dest_source, kAllowBaseToDerived,
                                        std::move(cb));
               });
}

// Implements the cast once references have been followed.
void DoCastExprValue(const fxl::RefPtr<EvalContext>& eval_context, CastType cast_type,
                     const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                     const ExprValueSource& dest_source, EvalCallback cb) {
  switch (cast_type) {
    case CastType::kImplicit:
      ImplicitCast(eval_context, source, dest_type, dest_source, std::move(cb));
      break;
    case CastType::kRust:  // TODO(sadmac): This is almost correct. Make sure it's exactly correct.
    case CastType::kC: {
      // A C-style cast can do the following things.
      //  - const_cast
      //  - static_cast
      //  - static_cast followed by a const_cast
      //  - reinterpret_cast
      //  - reinterpret_cast followed by a const_cast
      //
      // Since the debugger ignores const in debugging, this ends up being a static cast falling
      // back to a reinterpret cast.
      StaticCast(eval_context, source, dest_type, dest_source,
                 [eval_context, source, dest_type, dest_source,
                  cb = std::move(cb)](ErrOrValue result) mutable {
                   if (result.ok()) {
                     cb(result);
                   } else {
                     // StaticCast couldn't handle. Fall back on ReinterpretCast.
                     cb(ReinterpretCast(eval_context, source, dest_type, dest_source));
                   }
                 });
      break;
    }
    case CastType::kReinterpret:
      cb(ReinterpretCast(eval_context, source, dest_type, dest_source));
      break;
    case CastType::kStatic:
      StaticCast(eval_context, source, dest_type, dest_source, std::move(cb));
      break;
  }
}

}  // namespace

const char* CastTypeToString(CastType type) {
  switch (type) {
    case CastType::kImplicit:
      return "implicit";
    case CastType::kC:
      return "C";
    case CastType::kRust:
      return "Rust";
    case CastType::kReinterpret:
      return "reinterpret_cast";
    case CastType::kStatic:
      return "static_cast";
  }
  return "<invalid>";
}

void CastExprValue(const fxl::RefPtr<EvalContext>& eval_context, CastType cast_type,
                   const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                   const ExprValueSource& dest_source, EvalCallback cb) {
  if (CastShouldFollowReferences(eval_context, cast_type, source, dest_type)) {
    // Need to asynchronously follow the reference before doing the cast.
    EnsureResolveReference(eval_context, source,
                           [eval_context, cast_type, dest_type, cb = std::move(cb),
                            dest_source](ErrOrValue result) mutable {
                             if (result.has_error()) {
                               cb(result);
                             } else {
                               DoCastExprValue(eval_context, cast_type, result.value(), dest_type,
                                               dest_source, std::move(cb));
                             }
                           });
  } else {
    // Non-reference value, can cast right away.
    DoCastExprValue(eval_context, cast_type, source, dest_type, dest_source, std::move(cb));
  }
}

ErrOrValue CastNumericExprValue(const fxl::RefPtr<EvalContext>& eval_context,
                                const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                                const ExprValueSource& dest_source) {
  // Prevent crashes if we get bad types with no size.
  if (source.data().size() == 0 || dest_type->byte_size() == 0)
    return Err("Type has 0 size.");

  // Get the types without "const", etc. modifiers.
  fxl::RefPtr<Type> concrete_from = eval_context->GetConcreteType(source.type());
  fxl::RefPtr<Type> concrete_to = eval_context->GetConcreteType(dest_type.get());

  return NumericImplicitCast(source, dest_type, concrete_to.get(), concrete_from.get(),
                             dest_source);
}

}  // namespace zxdb
