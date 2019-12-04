// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_H_

#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/symbols/dwarf_lang.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ArrayType;
class BaseType;
class CodeBlock;
class Collection;
class CompileUnit;
class DataMember;
class ElfSymbol;
class Enumeration;
class Function;
class FunctionType;
class InheritedFrom;
class MemberPtr;
class ModifiedType;
class ModuleSymbols;
class Namespace;
class ProcessSymbols;
class TemplateParameter;
class Type;
class Value;
class Variable;
class Variant;
class VariantPart;

// Represents the type of a variable. This is a deserialized version of the various DWARF DIEs
// ("Debug Information Entry" -- a record in the DWARF file) that define types.
//
// SYMBOL MEMORY MODEL
// -------------------
// Symbols are reference counted and have references to other Symbols via a LazySymbol object which
// allows lazy decoding of the DWARF data. These are not cached or re-used so we can get many
// duplicate Symbol objects for the same DIE. Therefore, Symbol object identity is not a way to
// compare two symbols. Even if these were unified, DWARF will often encode the same thing in each
// compilation unit it is needed in, so object identity can never work in DWARF context.
//
// This non-caching behavior is important to prevent reference cycles that would cause memory leaks.
// Not only does each symbol reference its parent, there are complex and almost-arbitrary links
// between DIEs that don't work well with the reference-counting used by symbols.
//
// A downside to this design is that we might decode the same symbol multiple times and end up with
// many copies of the same data, both of which are inefficient.
//
// The main alternative would be to remove reference counting and instead maintain a per-module
// mapping of DIE address to decoded symbols. Then links between Symbol objects can either be DIE
// addresses that are looked up in the module every time they're needed (lets the module free things
// that haven't been used in a while) or object pointers (avoids the intermediate lookup but means
// objects can never be freed without unloading the whole module). This scheme would mean that
// symbols will be freed when the module is removed, which will require weak pointers from the
// expression system.
class Symbol : public fxl::RefCountedThreadSafe<Symbol> {
 public:
  DwarfTag tag() const { return tag_; }

  // The parent symbol.
  //
  // Normally this is the symbol that contains this one in the symbol file.
  //
  // In the case of function implementations with separate definitions, this will be the lexical
  // parent of the function (for example, a class or namespace) rather than the one containing the
  // code. This is how callers can navigate the type tree but it means the parent won't match the
  // record in the DWARF file.
  //
  // For inline functions, it's important to know both the lexical scope which tells you the
  // class/namespace of the function being inlined (the parent()) as well as the function it's
  // inlined into. Function symbols have a special containing_block() to give the latter.
  const UncachedLazySymbol& parent() const { return parent_; }
  void set_parent(const UncachedLazySymbol& e) { parent_ = e; }

  // Returns the name associated with this symbol. This name comes from the corresponding record in
  // the DWARF format (hence "assigned"). It will NOT include namespace and struct qualifiers.
  // Anything without a name assigned on the particular DWARF record name will return an empty
  // string, even if that thing logically has a name that can be computed (as for ModifiedType).
  //
  // This default implementation returns a reference to an empty string.  Derived classes will
  // override as needed.
  //
  // Most callers will want to use GetFullName().
  virtual const std::string& GetAssignedName() const;

  // Returns the full user-visible name for this symbol. This will include all namespace and struct
  // qualifications, and will include things like const and "*" qualifiers on modified types.
  //
  // It will not include a global qualifier ("::" at the beginning) because that's not desired in
  // most uses. If your use-case cares about controlling this, use GetIdentifier().
  //
  // This implements caching. Derived classes override ComputeFullName() to control how the full
  // name is presented.
  //
  // See also GetIdentifier().
  const std::string& GetFullName() const;

  // Returns the name of this symbol as an identifier if possible.
  //
  // Many symbols have identifier names, this normally includes anything with an assigned name:
  // functions, structs, typedefs and base types.
  //
  // Some things don't have names that can be made into identifiers, this includes modified types
  // such as "const Foo*" since the "const" and the "*" don't fit into the normal identifier scheme.
  // These types will report an empty Identifier for GetIdentifier().
  //
  // See also GetFullName(). GetFullName() will work for the modified type cases above since it just
  // returns a string, but it's not parseable.
  const Identifier& GetIdentifier() const;

  // Returns the CompileUnit that this symbol is associated with. Returns null on failure.
  fxl::RefPtr<CompileUnit> GetCompileUnit() const;

  // Returns the module symbols associated with this symbol object. It can be null if the module
  // has been unloaded and there are still dangling references to symbols, and it can also be null
  // in some test situations.
  virtual fxl::WeakPtr<ModuleSymbols> GetModuleSymbols() const;

  // Returns the symbol context for this symbol in the given process. This requires the process so
  // it can look up what the module load address is for this symbol's module (the same module can be
  // loaded into multiple processes).
  //
  // The ProcessSymbols can be null. It will be treated as an invalid module (see below).
  //
  // The module may not be valid. It could have been unloaded while there were dangling symbols,
  // or it can be null in some test situations. In these cases the resulting symbol context will
  // be a "relative" context -- see SymbolContext::is_relative().
  SymbolContext GetSymbolContext(const ProcessSymbols* process_symbols) const;

  // Computes and returns the language associated with this symbol. This will be kNone if the
  // language is not known or unset.
  //
  // This requires decoding the compile unit so is not super efficient to get.
  DwarfLang GetLanguage() const;

  // Manual RTTI.
  virtual const ArrayType* AsArrayType() const;
  virtual const BaseType* AsBaseType() const;
  virtual const CodeBlock* AsCodeBlock() const;
  virtual const Collection* AsCollection() const;
  virtual const CompileUnit* AsCompileUnit() const;
  virtual const DataMember* AsDataMember() const;
  virtual const ElfSymbol* AsElfSymbol() const;
  virtual const Enumeration* AsEnumeration() const;
  virtual const Function* AsFunction() const;
  virtual const FunctionType* AsFunctionType() const;
  virtual const InheritedFrom* AsInheritedFrom() const;
  virtual const MemberPtr* AsMemberPtr() const;
  virtual const ModifiedType* AsModifiedType() const;
  virtual const Namespace* AsNamespace() const;
  virtual const TemplateParameter* AsTemplateParameter() const;
  virtual const Type* AsType() const;
  virtual const Value* AsValue() const;
  virtual const Variable* AsVariable() const;
  virtual const Variant* AsVariant() const;
  virtual const VariantPart* AsVariantPart() const;

  // Non-const manual RTTI wrappers.
  ArrayType* AsArrayType() {
    return const_cast<ArrayType*>(const_cast<const Symbol*>(this)->AsArrayType());
  }
  BaseType* AsBaseType() {
    return const_cast<BaseType*>(const_cast<const Symbol*>(this)->AsBaseType());
  }
  CodeBlock* AsCodeBlock() {
    return const_cast<CodeBlock*>(const_cast<const Symbol*>(this)->AsCodeBlock());
  }
  Collection* AsCollection() {
    return const_cast<Collection*>(const_cast<const Symbol*>(this)->AsCollection());
  }
  CompileUnit* AsCompileUnit() {
    return const_cast<CompileUnit*>(const_cast<const Symbol*>(this)->AsCompileUnit());
  }
  DataMember* AsDataMember() {
    return const_cast<DataMember*>(const_cast<const Symbol*>(this)->AsDataMember());
  }
  ElfSymbol* AsElfSymbol() {
    return const_cast<ElfSymbol*>(const_cast<const Symbol*>(this)->AsElfSymbol());
  }
  Enumeration* AsEnumeration() {
    return const_cast<Enumeration*>(const_cast<const Symbol*>(this)->AsEnumeration());
  }
  Function* AsFunction() {
    return const_cast<Function*>(const_cast<const Symbol*>(this)->AsFunction());
  }
  FunctionType* AsFunctionType() {
    return const_cast<FunctionType*>(const_cast<const Symbol*>(this)->AsFunctionType());
  }
  InheritedFrom* AsInheritedFrom() {
    return const_cast<InheritedFrom*>(const_cast<const Symbol*>(this)->AsInheritedFrom());
  }
  MemberPtr* AsMemberPtr() {
    return const_cast<MemberPtr*>(const_cast<const Symbol*>(this)->AsMemberPtr());
  }
  ModifiedType* AsModifiedType() {
    return const_cast<ModifiedType*>(const_cast<const Symbol*>(this)->AsModifiedType());
  }
  Namespace* AsNamespace() {
    return const_cast<Namespace*>(const_cast<const Symbol*>(this)->AsNamespace());
  }
  TemplateParameter* AsTempalteParameter() {
    return const_cast<TemplateParameter*>(const_cast<const Symbol*>(this)->AsTemplateParameter());
  }
  Type* AsType() { return const_cast<Type*>(const_cast<const Symbol*>(this)->AsType()); }
  Value* AsValue() { return const_cast<Value*>(const_cast<const Symbol*>(this)->AsValue()); }
  Variable* AsVariable() {
    return const_cast<Variable*>(const_cast<const Symbol*>(this)->AsVariable());
  }
  Variant* AsVariant() {
    return const_cast<Variant*>(const_cast<const Symbol*>(this)->AsVariant());
  }
  VariantPart* AsVariantPart() {
    return const_cast<VariantPart*>(const_cast<const Symbol*>(this)->AsVariantPart());
  }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Symbol);
  FRIEND_MAKE_REF_COUNTED(Symbol);

  // Construct via fxl::MakeRefCounted.
  Symbol();
  explicit Symbol(DwarfTag tag);
  virtual ~Symbol();

  // Computes the full name and identifier. Used by GetFullName() and GetIdentifier() which add a
  // caching layer.
  //
  // Derived classes should override these to control how the name is presented. The default
  // implementation of ComputeIdentifier() returns the scope prefix (namespaces, structs) + the
  // assigned name. The default implementation of ComputeFullName() returns the stringified version
  // of the identifier.
  //
  // The returned Identifier should be globally qualified.
  virtual std::string ComputeFullName() const;
  virtual Identifier ComputeIdentifier() const;

 private:
  DwarfTag tag_ = DwarfTag::kNone;

  // Using the "uncached" version here prevents reference cycles since normally a parent has
  // references back to each of its children. By always using the "uncached" one when pointing
  // up in the symbol tree, there are no owning references to symbol objects going in the opposite
  // direction that can cause reference cycles. The tradeoff is that going up in the tree requires
  // decoding the symbol each time at a slight performance penalty.
  UncachedLazySymbol parent_;

  // Lazily computed full symbol name and identifier name.
  mutable std::optional<std::string> full_name_;
  mutable std::optional<Identifier> identifier_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SYMBOL_H_
