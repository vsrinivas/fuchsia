// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_

#include <cassert>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>

#include "source_span.h"
#include "token.h"
#include "types.h"
#include "utils.h"

namespace fidl {

// Forward decls
namespace raw {
class AttributeList;
}  // namespace raw

namespace flat {
struct Attribute;
struct AttributeArg;
struct Constant;
struct Decl;
struct Type;
class TypeTemplate;
class Name;
}  // namespace flat

namespace internal {

constexpr std::string_view kFormatMarker = "{}";

std::string Display(const std::string& s);
std::string Display(std::string_view s);
std::string Display(const std::set<std::string>& s);
std::string Display(const SourceSpan& s);
std::string Display(const Token::KindAndSubkind& t);
std::string Display(const types::Openness o);
std::string Display(const raw::AttributeList* a);
std::string Display(const std::vector<std::string_view>& library_name);
std::string Display(const flat::Attribute* a);
std::string Display(const flat::AttributeArg* a);
std::string Display(const flat::Constant* c);
std::string Display(const flat::Decl* d);
std::string Display(std::vector<const flat::Decl*>& d);
std::string Display(const flat::Type* t);
std::string Display(const flat::TypeTemplate* t);
std::string Display(const flat::Name& n);
template <typename T, typename = decltype(std::to_string(std::declval<T>()))>
std::string Display(T val) {
  return std::to_string(val);
}

inline void FormatHelper(std::stringstream& out, std::string_view msg) {
  assert(msg.find(kFormatMarker) == std::string::npos);
  out << msg;
}

template <typename T, typename... Rest>
void FormatHelper(std::stringstream& out, std::string_view msg, T t, const Rest&... rest) {
  auto i = msg.find(kFormatMarker);
  assert(i != std::string::npos);
  out << msg.substr(0, i) << Display(t);
  auto remaining_msg = msg.substr(i + kFormatMarker.size());
  FormatHelper(out, remaining_msg, rest...);
}

template <typename... Args>
std::string FormatDiagnostic(std::string_view msg, const Args&... args) {
  std::stringstream s;
  FormatHelper(s, msg, args...);
  return s.str();
}

constexpr size_t CountFormatArgs(std::string_view s) {
  size_t i = s.find(kFormatMarker, 0);
  size_t total = 0;
  while (i != std::string::npos) {
    total++;
    i = s.find(kFormatMarker, i + kFormatMarker.size());
  }
  return total;
}

template <typename... Args>
constexpr void CheckFormatArgs(std::string_view msg) {
  static_assert(
      (std::is_same_v<Args, std::remove_const_t<std::remove_reference_t<Args>>> && ...),
      "Remove redundant `const` or `&`; DiagnosticDef args are always passed by const reference");
  static_assert(((!std::is_pointer_v<Args> || std::is_const_v<std::remove_pointer_t<Args>>)&&...),
                "Use a const pointer; DiagnosticDef args should not be mutable pointers");
  static_assert(((!std::is_same_v<Args, std::string>)&&...),
                "Use std::string_view, not std::string");

  // This can't be a static_assert because the compiler doesn't know msg is
  // always constexpr. If the condition is true, the assert evaluates to a
  // no-op. If the condition is false, it evaluates to a (non-constexpr) abort,
  // resulting in a "must be initialized by a constant expression" error.
  assert(sizeof...(Args) == internal::CountFormatArgs(msg) &&
         "Number of format string parameters '{}' != number of template arguments");
}

}  // namespace internal

using utils::identity_t;

struct DiagnosticDef {
  constexpr explicit DiagnosticDef(std::string_view msg) : msg(msg) {}
  DiagnosticDef(const DiagnosticDef&) = delete;

  std::string_view msg;
};

// The definition of an error. All instances of ErrorDef are in diagnostics.h.
// Template args define format parameters in the error message.
template <typename... Args>
struct ErrorDef final : DiagnosticDef {
  constexpr explicit ErrorDef(std::string_view msg) : DiagnosticDef(msg) {
    internal::CheckFormatArgs<Args...>(msg);
  }
};

// The definition of a warning. All instances of WarningDef are in
// diagnostics.h. Template args define format parameters in the warning message.
template <typename... Args>
struct WarningDef final : DiagnosticDef {
  constexpr explicit WarningDef(std::string_view msg) : DiagnosticDef(msg) {
    internal::CheckFormatArgs<Args...>(msg);
  }
};

// A tag that indicates whether a diagnostic is an error or warning. In the
// future this could be extended to include hints, suggestions, etc.
enum class DiagnosticKind {
  kError,
  kWarning,
};

// A Diagnostic is the result of instantiating a DiagnosticDef with arguments.
// It stores a formatted std::string where "{}" markers have been replaced by
// arguments. It also stores a SourceSpan indicating where the problem occurred.
struct Diagnostic {
  template <typename... Args>
  Diagnostic(DiagnosticKind kind, const DiagnosticDef& def, SourceSpan span, const Args&... args)
      : kind(kind), def(def), span(span), msg(internal::FormatDiagnostic(def.msg, args...)) {}
  Diagnostic(const Diagnostic&) = delete;

  // The factory functions below could be constructors, and std::make_unique
  // would work fine. However, template error messages are better with static
  // functions because it doesn't have to try every constructor.

  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeError(const ErrorDef<Args...>& def, SourceSpan span,
                                               const identity_t<Args>&... args) {
    return std::make_unique<Diagnostic>(DiagnosticKind::kError, def, span, args...);
  }

  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeWarning(const WarningDef<Args...>& def, SourceSpan span,
                                                 const identity_t<Args>&... args) {
    return std::make_unique<Diagnostic>(DiagnosticKind::kWarning, def, span, args...);
  }

  DiagnosticKind kind;
  const DiagnosticDef& def;
  SourceSpan span;
  std::string msg;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_
