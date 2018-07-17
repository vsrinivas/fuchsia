// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/lazy_symbol.h"
#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class BaseType;
class Function;
class ModifiedType;
class Type;

// Represents the type of a variable. This is a deserialized version of the
// various DWARF DIE entries that define types. It is normally generated from
// a LazySymbol from a DIE reference.
class Symbol : public fxl::RefCountedThreadSafe<Symbol> {
 public:
  // Not a DWARF tag, this is used to indicate "not present."
  static constexpr int kTagNone = 0x00;

  // Type modifier for arrays ("foo[]") of an underlying type. May have a
  // SubrangeType child that indicates the size of the array.
  static constexpr int kTagArray = 0x01;

  // C++ class definition.
  static constexpr int kTagClass = 0x02;

  // "Alterate entry point" to a function. Seems to be not generated.
  static constexpr int kTagEntryPoint = 0x03;

  // C/C++ "enum" declaration. May have children of kTagEnumerator.
  static constexpr int kTagEnumerationType = 0x04;

  // Normal function parameter, seen as a child of a "subprogram." It will
  // normally have at least a name and a type.
  static constexpr int kTagFormalParameter = 0x05;

  // Generated for "using" statements that bring a type into a namespace.
  // Converted into a TypeModifier class.
  static constexpr int kTagImportedDeclaration = 0x08;

  // Label (as used for "goto"). Probably don't need to handle.
  static constexpr int kTagLabel = 0x0a;

  // A lexical block will typically have children of kTagVariable for
  // everything declared in it. It will also often have ranges associated with
  // it.
  static constexpr int kTagLexicalBlock = 0x0b;

  // Class member data.
  static constexpr int kTagMember = 0x0d;

  // Type modifier that indicates a pointer to an underlying type.
  static constexpr int kTagPointerType = 0x0f;

  // Type modifier that indicates a reference to an underlying type.
  static constexpr int kTagReferenceType = 0x10;

  static constexpr int kTagCompileUnit = 0x11;

  // Not used in C/C++ (they don't have a true primitive string type).
  static constexpr int kTagStringType = 0x12;

  // C/C++ struct declaration.
  static constexpr int kTagStructureType = 0x13;

  // Type for a C/C++ pointer to member function. See kTagPtrToMemberType.
  static constexpr int kTagSubroutineType = 0x15;

  // Typedef that provides a different name for an underlying type. Converted
  // into a TypeModifier class.
  static constexpr int kTagTypedef = 0x16;

  static constexpr int kTagUnionType = 0x17;

  // Indicates a C/C++ parameter of "...".
  static constexpr int kTagUnspecifiedParameters = 0x18;

  static constexpr int kTagVariant = 0x19;

  // Common block and common inclusion are used by Fortran. Can ignore.
  static constexpr int kTagCommonBlock = 0x1a;
  static constexpr int kTagCommonInclusion = 0x1b;

  // A member of a class or struct that indicates a type it inherits from.
  static constexpr int kTagInheritance = 0x1c;

  // Child of a subroutine indicating a section of code that's from another
  // subroutine that's been inlined.
  static constexpr int kTagInlinedSubroutine = 0x1d;

  static constexpr int kTagModule = 0x1e;

  // C++ Foo::* type. See kTagSubroutineType.
  static constexpr int kTagPtrToMemberType = 0x1f;

  // Used by Pascal. Can ignore.
  static constexpr int kTagSetType = 0x20;

  // In C++ this can be generated as the child of an array entry with a "type"
  // of "__ARRAY_SIZE_TYPE__" and a "count" indicating the size of the array.
  static constexpr int kTagSubrangeType = 0x21;

  // Pascal and Modula-2 "with" statement. Can ignore.
  static constexpr int kTagWithStmt = 0x22;

  // C++ "public", "private", "protected". Seems to not be generated.
  static constexpr int kTagAccessDeclaration = 0x23;

  // Declaration of a built-in compiler base type like an "int".
  static constexpr int kTagBaseType = 0x24;

  static constexpr int kTagCatchBlock = 0x25;

  // Type modifier that adds "const".
  static constexpr int kTagConstType = 0x26;

  // Named constant.
  static constexpr int kTagConstant = 0x27;

  // Member of an enumeration. Will be a child of an EnumerationType entry.
  static constexpr int kTagEnumerator = 0x28;

  static constexpr int kTagFileType = 0x29;

  // C++ "friend" declaration. Seems to not be generated.
  static constexpr int kTagFriend = 0x2a;

  // Namelists are used in Fortran 90. Can ignore.
  static constexpr int kTagNamelist = 0x2b;
  static constexpr int kTagNamelistItem = 0x2c;

  // Packed types are used only by Pascal and ADA. Can ignore.
  static constexpr int kTagPackedType = 0x2d;

  // A function. Represented by a zxdb::Function object.
  static constexpr int kTagSubprogram = 0x2e;
  static constexpr int kTagTemplateTypeParameter = 0x2f;
  static constexpr int kTagTemplateValueParameter = 0x30;
  static constexpr int kTagThrownType = 0x31;
  static constexpr int kTagTryBlock = 0x32;
  static constexpr int kTagVariantPart = 0x33;

  // Local variable declaration. It will normally have a name, type,
  // declaration location, and location.
  static constexpr int kTagVariable = 0x34;

  // Type modifier that indicates aadding "volatile" to an underlying type.
  static constexpr int kTagVolatileType = 0x35;
  static constexpr int kTagDwarfProcedure = 0x36;

  // Type modifier that indicates a C99 "restrict" qualifier on an underlying
  // type.
  static constexpr int kTagRestrictType = 0x37;

  // Java interface. Can ignore.
  static constexpr int kTagInterfaceType = 0x38;

  // C++ namespace. The declarations inside this will be the contents of the
  // namespace. This will be around declarations but not necessarily the
  // function implementations.
  static constexpr int kTagNamespace = 0x39;

  // Seems to be generated for "using namespace" statements.
  static constexpr int kTagImportedModule = 0x3a;

  // Used in our toolchain for "decltype(nullptr)".
  static constexpr int kTagUnspecifiedType = 0x3b;

  static constexpr int kTagPartialUnit = 0x3c;
  static constexpr int kTagImportedUnit = 0x3d;

  // "If" statement. Seems to not be generated by our toolchain.
  static constexpr int kTagCondition = 0x3f;

  // Used by the "UPC" language. Can ignore.
  static constexpr int kTagSharedType = 0x40;

  // Seems to not be generated by our toolchain.
  static constexpr int kTagTypeUnit = 0x41;

  // Type modifier that indicates an rvalue reference to an underlying type.
  static constexpr int kTagRvalueReferenceType = 0x42;
  static constexpr int kTagTemplateAlias = 0x43;

  // User-defined range.
  static constexpr int kTagLoUser = 0x4080;
  static constexpr int kTagHiUser = 0xffff;

  // One of kTag* constants above, or something totally different (depending
  // on what's in the symbol file).
  int tag() const { return tag_; }

  // Manual RTTI.
  virtual const BaseType* AsBaseType() const;
  virtual const Function* AsFunction() const;
  virtual const ModifiedType* AsModifiedType() const;
  virtual const Type* AsType() const;

  // Non-const manual RTTI wrappers.
  BaseType* AsBaseType() {
    return const_cast<BaseType*>(const_cast<const Symbol*>(this)->AsBaseType());
  }
  Function* AsFunction() {
    return const_cast<Function*>(const_cast<const Symbol*>(this)->AsFunction());
  }
  ModifiedType* AsModifiedType() {
    return const_cast<ModifiedType*>(
        const_cast<const Symbol*>(this)->AsModifiedType());
  }
  Type* AsType() {
    return const_cast<Type*>(const_cast<const Symbol*>(this)->AsType());
  }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Symbol);
  FRIEND_MAKE_REF_COUNTED(Symbol);

  // Construct via fxl::MakeRefCounted.
  Symbol();
  explicit Symbol(int tag);
  virtual ~Symbol();

 private:
  int tag_ = kTagNone;
};

}  // namespace
