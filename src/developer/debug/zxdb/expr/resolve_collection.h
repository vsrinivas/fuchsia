// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_COLLECTION_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_COLLECTION_H_

#include <optional>
#include <string>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/found_member.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class CodeBlock;
class EvalContext;
class ExprValue;
class FoundMember;
class InheritedFrom;

// Resolves a member given a collection (class/struct/union) and either a record for a variable
// within that collection (in this case the data member must be on the class itself, not on a base
// class), or a name of a member.
//
// These will be synchronous in most cases, but resolving static members may require requesting the
// memory from the target which will force an asynchronous result.
//
// The FoundMember may have no data member in it. If so, calls the callback with an error (this is
// so callers don't have to type check the inputs).
void ResolveMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                   const FoundMember& member, EvalCallback cb);
void ResolveMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                   const ParsedIdentifier& identifier, EvalCallback cb);

// Synchronous versions of ResolveMember for cases where the value is known not to be an extern
// (static) member or on a derived class. This is generally used when hardcoding support for known
// structures.
//
// The variant that takes an initializer list will interpret the strings as identifiers, parse
// them, and resolve a nested series of members using those strings. For example, if the input
// is {"a", "b"} this will resolve "base.a.b". This is used for hardcoding some printers.
ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  const FoundMember& member);
ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  const ParsedIdentifier& identifier);
ErrOrValue ResolveNonstaticMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                  std::initializer_list<std::string> names);

// The variant takes an ExprValue which is a pointer to the base/struct or class. Because it fetches
// memory it is always asynchronous.
//
// Since it's given a FoundMember, this can not check for members of derived classes. Use the
// version that takes an Identifier if you want this capability.
void ResolveMemberByPointer(const fxl::RefPtr<EvalContext>& context, const ExprValue& base_ptr,
                            const FoundMember& found_member, EvalCallback cb);

// Same as previous version but takes the name of the member to find. The callback also provides the
// FoundMember corresponding to what the name matched.
//
// This also supports (when requested by the EvalContext) automatically converting base class
// pointers to derived class pointers when the derived class is known. It allows "foo->bar" where
// "bar" is a data member on the current derived class's instance of foo.
void ResolveMemberByPointer(const fxl::RefPtr<EvalContext>& context, const ExprValue& base_ptr,
                            const ParsedIdentifier& identifier,
                            fit::callback<void(ErrOrValue, const FoundMember&)> cb);

// Takes a Collection value and a base class inside of it, computes the value of the base class and
// puts it in *out.
//
// For the version that takes an InheritedFrom, the base class must be a direct base class of the
// "value" collection, not an indirect base.
// TODO(brettw) this variant should take a FoundMember instead.
//
// The asynchronous version supports virtual inheritance, while the other variants do not. The
// from_symbol_context should be the symbol context associated with the module from which the
// InheritedFrom object came. It will be used to evaluate the location expression.
//
// For the version that takes a type and an offset, the type must already have been computed as some
// type of base class that lives at the given offset. It need not be a direct base and no type
// checking is done as long as the offsets and sizes are valid.
void ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                      const InheritedFrom* from, const SymbolContext& from_symbol_context,
                      fit::callback<void(ErrOrValue)> cb);
ErrOrValue ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                            const InheritedFrom* from);
ErrOrValue ResolveInherited(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                            fxl::RefPtr<Type> base_type, uint64_t offset);

// Verifies that |input| type is a pointer to a collection and fills the pointed-to type into
// |*pointed_to|. In other cases, returns an error. The input type can be null (which will produce
// an error) or non-concrete (const, forward definition, etc.) so the caller doesn't have to check.
//
// The returned type will be concrete which means the type may be modified to strip CV qualifiers.
// This is used when looking up collection members by pointer so this is needed. It should not be
// used to generate types that might be visible to the user (they'll want the qualifiers).
Err GetConcretePointedToCollection(const fxl::RefPtr<EvalContext>& eval_context, const Type* input,
                                   fxl::RefPtr<Collection>* pointed_to);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_COLLECTION_H_
