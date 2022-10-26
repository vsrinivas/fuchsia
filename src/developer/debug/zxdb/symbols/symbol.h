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
#include "src/developer/debug/zxdb/symbols/dwarf_unit.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ArrayType;
class BaseType;
class CallSite;
class CallSiteParameter;
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
//
// DIE ADDRESSES AND COMPILATION UNITS
// -----------------------------------
// In LLVM a DIE is indexed by its llvm::DWARFUnit and an offset within that. In contrast, our
// LazySymbols use a single global index which is converted to a unit/offset as needed. Once we
// decode a DIE, we don't need the offset at all, and we never needed the unit, so there are not
// available.
//
// If we find we need this information on each symbol in the future, it could be added. We would
// want to add some caching system since currently make duplicate DwarfUnit objects for the same
// LLVM One.
//
// Currently the DwarfUnit is accessible by waking up the tree to the CompileUnit. The CompileUnit
// stores a DwarfUnit pointer. Note that the CompileUnit is a DIE symbol while the DwarfUnit is the
// container for the CompileUnit and everything else associated with an object file. Many offsets
// in the symbols are relative to the DwarfUnit (note the main die_offset() is module-global).
class Symbol : public fxl::RefCountedThreadSafe<Symbol> {
 public:
  // Returns a lazy reference to this symbol. When creating LazySymbols, be sure not to store it
  // in such a way that it could create a reference cycle (so do not save it in any children of
  // this symbol or in anything it references).
  //
  // Note that we don't provide a GetUncachedLazySymbol() variant. That would be easy but is not
  // currently needed and it's potentially slightly dangerous. The uncached variants are used to
  // avoid reference cycles, but if we have a test object, it will contain a hard reference. The
  // tests use some helpers to clean this up safely (the SymbolTestParentSetter). Returning an
  // UncachedLazySymbol here may create the impression that it can be used in a "parent" context
  // while this would not be safe for test data.
  LazySymbol GetLazySymbol() const;

  // Sets the symbol factory pointer and DIE offset for this symbol (returned by GetDieOffset() and
  // GetLazySymbol(), see those for more).
  //
  // It would intuitively make the most sense for this to be set in the constructor since it's a
  // fundamental property of the symbol.
  //
  // The majority of symbols in production are created by the DwarfSymbolFactory, but the majority
  // of call sites that create symbols (by ~2 orders of magnitude) are tests. Defaulting the
  // factory/offset info and having a setter allows the DwarfSymbolFactory to set them while keeping
  // the test call sites cleaner.
  void set_lazy_this(UncachedLazySymbol lazy) { lazy_this_ = std::move(lazy); }

  // Global offset of this symbol within the module. This can be 0 for most symbols created in tests
  // and for synthetic symbols like the built-in "int" type generated by the expression system.
  // This is mostly useful when doing low-level symbol operations and interacting with LLVM.
  //
  // This offset is set by set_lazy_this().
  uint64_t GetDieOffset() const;

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

  // Returns the DwarfUnit or CompileUnit that this symbol is associated with. Returns null on
  // failure. See the comment at the top of this class and above the DwarfUnit declaration for more.
  fxl::RefPtr<CompileUnit> GetCompileUnit() const;
  fxl::RefPtr<DwarfUnit> GetDwarfUnit() const;

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

  // Allows templatized conversion to a base class. Both const and non-const variants are supported
  // using the protected virtual functions below. This is basically dynamic_cast but we're required
  // to avoid compiling with RTTI.
  //
  //   const Collection* c = symbol->As<Collection>();
  //   if (!c)
  //     return false;
  //
  template <typename Derived>
  const Derived* As() const;

  template <typename Derived>
  Derived* As();

#define IMPLEMENT_TEMPLATIZED_AS(DerivedType)                                            \
  template <>                                                                            \
  const DerivedType* As<DerivedType>() const {                                           \
    return As##DerivedType();                                                            \
  }                                                                                      \
  template <>                                                                            \
  DerivedType* As<DerivedType>() {                                                       \
    return const_cast<DerivedType*>(const_cast<const Symbol*>(this)->As##DerivedType()); \
  }

  IMPLEMENT_TEMPLATIZED_AS(ArrayType)
  IMPLEMENT_TEMPLATIZED_AS(BaseType)
  IMPLEMENT_TEMPLATIZED_AS(CallSite)
  IMPLEMENT_TEMPLATIZED_AS(CallSiteParameter)
  IMPLEMENT_TEMPLATIZED_AS(CodeBlock)
  IMPLEMENT_TEMPLATIZED_AS(Collection)
  IMPLEMENT_TEMPLATIZED_AS(CompileUnit)
  IMPLEMENT_TEMPLATIZED_AS(DataMember)
  IMPLEMENT_TEMPLATIZED_AS(ElfSymbol)
  IMPLEMENT_TEMPLATIZED_AS(Enumeration)
  IMPLEMENT_TEMPLATIZED_AS(Function)
  IMPLEMENT_TEMPLATIZED_AS(FunctionType)
  IMPLEMENT_TEMPLATIZED_AS(InheritedFrom)
  IMPLEMENT_TEMPLATIZED_AS(MemberPtr)
  IMPLEMENT_TEMPLATIZED_AS(ModifiedType)
  IMPLEMENT_TEMPLATIZED_AS(Namespace)
  IMPLEMENT_TEMPLATIZED_AS(TemplateParameter)
  IMPLEMENT_TEMPLATIZED_AS(Type)
  IMPLEMENT_TEMPLATIZED_AS(Value)
  IMPLEMENT_TEMPLATIZED_AS(Variable)
  IMPLEMENT_TEMPLATIZED_AS(Variant)
  IMPLEMENT_TEMPLATIZED_AS(VariantPart)

#undef IMPLEMENT_TEMPLATIZED_AS

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Symbol);
  FRIEND_MAKE_REF_COUNTED(Symbol);

  // Construct via fxl::MakeRefCounted.
  Symbol();
  explicit Symbol(DwarfTag tag);
  virtual ~Symbol();

  // Manual RTTI. See "As<...>" template above for the public versions (these are protected just
  // so callers are consistent).
  virtual const ArrayType* AsArrayType() const;
  virtual const BaseType* AsBaseType() const;
  virtual const CallSite* AsCallSite() const;
  virtual const CallSiteParameter* AsCallSiteParameter() const;
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
  UncachedLazySymbol lazy_this_;  // See set_lazy_this() above.
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
