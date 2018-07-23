// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"

#include "garnet/bin/zxdb/client/symbols/namespace.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/type.h"
#include "lib/fxl/logging.h"

namespace zxdb {

// Returns the effective symbol name for showing to the user. This special-
// cases asnonymous namespaces to print "(anon)".
const std::string& GetVisibleSymbolName(const Symbol* symbol) {
  static const std::string kAnon("(anon)");

  if (const Namespace* ns = symbol->AsNamespace()) {
    const std::string& ns_name = ns->GetAssignedName();
    if (ns_name.empty())
      return kAnon;
    else
      return ns_name;
  }
  return symbol->GetAssignedName();
}

// Some things in this file currently hardcode C/C++ rules. To support other
// languages, we will need to extract the language from the compilation unit
// and dispatch to a different implementation.

std::vector<fxl::RefPtr<const Symbol>> GetSymbolScope(const Symbol* symbol) {
  std::vector<fxl::RefPtr<const Symbol>> result;

  const Symbol* prev = symbol;
  while (prev->parent().is_valid()) {
    const Symbol* cur = prev->parent().Get();
    // TODO(brettw) make sure enums (classes vs. not) are handled correctly.
    if (const Namespace* ns = cur->AsNamespace()) {
      result.emplace(result.begin(), cur);
    } else if (const StructClass* sc = cur->AsStructClass()) {
      result.emplace(result.begin(), cur);
    } else if (cur->AsFunction() || cur->tag() == Symbol::kTagCompileUnit) {
      // Stop qualifying names at function boundaries. We will need to have
      // a special way to name symbols locally defined in a function.
      // For a struct "Baz" locally defined in a function "GetFoo(Foo)" inside
      // an anonymous namespace, GDB and LLDB show it as:
      //   (anonymous namespace)::GetFoo((anonymous namespace)::Foo)::Baz
      break;
    }
    // Anything else (e.g. lexical blocks) just get skipped.

    prev = cur;
  }
  return result;
}

std::string SymbolScopeToPrefixString(
    const std::vector<fxl::RefPtr<const Symbol>>& scope) {
  static const char kCppSeparator[] = "::";

  std::string result;
  for (const auto& symbol : scope) {
    // TODO(brettw) make sure enums (classes vs. not) are handled correctly.
    result.append(GetVisibleSymbolName(symbol.get()));
    result.append(kCppSeparator);
  }
  return result;
}

std::string GetSymbolScopePrefix(const Symbol* symbol) {
  return SymbolScopeToPrefixString(GetSymbolScope(symbol));
}

std::string GetFullyQualifiedSymbolName(const Symbol* symbol) {
  if (const Type* type = symbol->AsType())
    return type->GetTypeName();
  return GetSymbolScopePrefix(symbol) + GetVisibleSymbolName(symbol);
}

}  // namespace zxdb
