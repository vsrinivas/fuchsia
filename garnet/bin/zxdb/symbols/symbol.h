// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/symbols/dwarf_tag.h"
#include "garnet/bin/zxdb/symbols/lazy_symbol.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class ArrayType;
class BaseType;
class CodeBlock;
class Collection;
class DataMember;
class Enumeration;
class Function;
class FunctionType;
class InheritedFrom;
class MemberPtr;
class ModifiedType;
class Namespace;
class Type;
class Value;
class Variable;

// Represents the type of a variable. This is a deserialized version of the
// various DWARF DIEs ("Debug Information Entry" -- a record in the DWARF file)
// that define types.
//
// SYMBOL MEMORY MODEL
// -------------------
// Symbols are reference counted and have references to other Symbols via a
// LazySymbol object which allows lazy decoding of the DWARF data. These are
// not cached or re-used so we can get many duplicate Symbol objects for the
// same DIE. Therefore, Symbol object identity is not a way to compare two
// symbols. Even if these were unified, DWARF will often encode the same thing
// in each compilation unit it is needed in, so object identity can never work
// in DWARF context.
//
// This non-caching behavior is important to prevent reference cycles that
// would cause memory leaks. Not only does each symbol reference its parent,
// there are complex and almost-arbitrary links between DIEs that don't work
// well with the reference-counting used by symbols.
//
// A downside to this design is that we might decode the same symbol multiple
// times and end up with many copies of the same data, both of which are
// inefficient.
//
// The main alternative would be to remove reference counting and instead
// maintain a per-module mapping of DIE address to decoded symbols. Then links
// between Symbol objects can either be DIE addresses that are looked up in the
// module every time they're needed (lets the module free things that haven't
// been used in a while) or object pointers (avoids the intermediate lookup but
// means objects can never be freed without unloading the whole module). This
// scheme would mean that symbols will be freed when the module is removed,
// which will require weak pointers from the expression system.
class Symbol : public fxl::RefCountedThreadSafe<Symbol> {
 public:
  DwarfTag tag() const { return tag_; }

  // The parent symbol.
  //
  // Normally this is the symbol that contains this one in the symbol file.
  //
  // In the case of function implementations with separate definitions, this
  // will be the lexical parent of the function (for example, a class or
  // namespace) rather than the one containing the code. This is how callers
  // can navigate the type tree but it means the parent won't match the
  // record in the DWARF file.
  //
  // For inline functions, it's important to know both the lexical scope
  // which tells you the class/namespace of the function being inlined (the
  // parent()) as well as the function it's inlined into. Function symbols have
  // a special containing_block() to give the latter.
  const LazySymbol& parent() const { return parent_; }
  void set_parent(const LazySymbol& e) { parent_ = e; }

  // Returns the name associated with this symbol. This name comes from the
  // corresponding record in the DWARF format (hence "assigned"). It will NOT
  // include namespace and struct qualifiers. Anything without a name assigned
  // on the particular DWARF record name will return an empty string, even if
  // that thing logically has a name that can be computed (as for
  // ModifiedType).
  //
  // This default implementation returns a reference to an empty string.
  // Derived classes will override as needed.
  //
  // Most callers will want to use GetFullName().
  virtual const std::string& GetAssignedName() const;

  // Returns the fully-qualified user-visible name for this symbol. This will
  // include all namespace and struct qualifications.
  //
  // This implements caching. Derived classes override ComputeFullName() to
  // control how the full name is presented.
  const std::string& GetFullName() const;

  // Manual RTTI.
  virtual const ArrayType* AsArrayType() const;
  virtual const BaseType* AsBaseType() const;
  virtual const CodeBlock* AsCodeBlock() const;
  virtual const DataMember* AsDataMember() const;
  virtual const Enumeration* AsEnumeration() const;
  virtual const Function* AsFunction() const;
  virtual const FunctionType* AsFunctionType() const;
  virtual const InheritedFrom* AsInheritedFrom() const;
  virtual const MemberPtr* AsMemberPtr() const;
  virtual const ModifiedType* AsModifiedType() const;
  virtual const Namespace* AsNamespace() const;
  virtual const Collection* AsCollection() const;
  virtual const Type* AsType() const;
  virtual const Value* AsValue() const;
  virtual const Variable* AsVariable() const;

  // Non-const manual RTTI wrappers.
  ArrayType* AsArrayType() {
    return const_cast<ArrayType*>(
        const_cast<const Symbol*>(this)->AsArrayType());
  }
  BaseType* AsBaseType() {
    return const_cast<BaseType*>(const_cast<const Symbol*>(this)->AsBaseType());
  }
  CodeBlock* AsCodeBlock() {
    return const_cast<CodeBlock*>(
        const_cast<const Symbol*>(this)->AsCodeBlock());
  }
  DataMember* AsDataMember() {
    return const_cast<DataMember*>(
        const_cast<const Symbol*>(this)->AsDataMember());
  }
  Enumeration* AsEnumeration() {
    return const_cast<Enumeration*>(
        const_cast<const Symbol*>(this)->AsEnumeration());
  }
  Function* AsFunction() {
    return const_cast<Function*>(const_cast<const Symbol*>(this)->AsFunction());
  }
  FunctionType* AsFunctionType() {
    return const_cast<FunctionType*>(
        const_cast<const Symbol*>(this)->AsFunctionType());
  }
  InheritedFrom* AsInheritedFrom() {
    return const_cast<InheritedFrom*>(
        const_cast<const Symbol*>(this)->AsInheritedFrom());
  }
  MemberPtr* AsMemberPtr() {
    return const_cast<MemberPtr*>(
        const_cast<const Symbol*>(this)->AsMemberPtr());
  }
  ModifiedType* AsModifiedType() {
    return const_cast<ModifiedType*>(
        const_cast<const Symbol*>(this)->AsModifiedType());
  }
  Namespace* AsNamespace() {
    return const_cast<Namespace*>(
        const_cast<const Symbol*>(this)->AsNamespace());
  }
  Collection* AsCollection() {
    return const_cast<Collection*>(
        const_cast<const Symbol*>(this)->AsCollection());
  }
  Type* AsType() {
    return const_cast<Type*>(const_cast<const Symbol*>(this)->AsType());
  }
  Value* AsValue() {
    return const_cast<Value*>(const_cast<const Symbol*>(this)->AsValue());
  }
  Variable* AsVariable() {
    return const_cast<Variable*>(const_cast<const Symbol*>(this)->AsVariable());
  }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Symbol);
  FRIEND_MAKE_REF_COUNTED(Symbol);

  // Construct via fxl::MakeRefCounted.
  Symbol();
  explicit Symbol(DwarfTag tag);
  virtual ~Symbol();

  // Computes the full name. Used by GetFullName() which adds a caching layer.
  // Derived classes should override this to control how the name is presented.
  // This implementation returns the scope prefix (namespaces, structs) +
  // assigned name.
  virtual std::string ComputeFullName() const;

 private:
  DwarfTag tag_ = DwarfTag::kNone;

  LazySymbol parent_;

  // Lazily computed full symbol name.
  // TODO(brettw) use std::optional when we can use C++17.
  mutable bool computed_full_name_ = false;
  mutable std::string full_name_;
};

}  // namespace zxdb
