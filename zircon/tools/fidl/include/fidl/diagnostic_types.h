// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_

#include <cassert>
#include <set>
#include <sstream>
#include <string_view>

#include "source_span.h"
#include "token.h"

namespace fidl {

// Forward decls
namespace raw {
class Attribute;
class AttributeList;
}  // namespace raw

namespace flat {
struct Constant;
struct IdentifierConstant;
struct LiteralConstant;
struct TypeConstructor;
struct Type;
class TypeTemplate;
class Name;
}  // namespace flat

namespace diagnostics {
namespace internal {

constexpr std::string_view kFormatMarker = "{}";

std::string Display(const std::string& s);
std::string Display(std::string_view s);
std::string Display(const std::set<std::string>& s);
std::string Display(const SourceSpan& s);
std::string Display(const Token::KindAndSubkind& t);
std::string Display(const raw::Attribute& a);
std::string Display(const raw::AttributeList& a);
std::string Display(const std::vector<std::string_view>& library_name);
std::string Display(const flat::Constant* c);
std::string Display(const flat::TypeConstructor* tc);
std::string Display(const flat::Type* t);
std::string Display(const flat::TypeTemplate* t);
std::string Display(const flat::Name& n);
template <typename T, typename = decltype(std::to_string(std::declval<T>()))>
std::string Display(T val) {
  return std::to_string(val);
}

inline std::string FormatErr(std::string_view msg) {
  // This assert should never fail, because FormatErr is only called by
  // ReportError -- and calls to ReportError fail at compile time if the # of
  // args passed in != the number of args in the Error definition.
  assert(msg.find(kFormatMarker) == std::string::npos &&
         "number of format string parameters '{}' != number of supplied arguments");
  return std::string(msg);
}

template <typename T, typename... Rest>
std::string FormatErr(std::string_view msg, T t, Rest... rest) {
  size_t i = msg.find(kFormatMarker);
  // This assert should never fail (see non-template FormatErr)
  assert(i != std::string::npos &&
         "number of format string parameters '{}' != number of supplied arguments");

  // Split string at marker, insert formatted parameter
  std::stringstream s;
  s << msg.substr(0, i) << Display(t)
    << msg.substr(i + kFormatMarker.length(), msg.length() - i - kFormatMarker.length());

  return FormatErr(s.str(), rest...);
}

constexpr size_t count_format_args(std::string_view s) {
  size_t i = s.find(kFormatMarker, 0);
  size_t total = 0;
  while (i != std::string::npos && i < s.size()) {
    total++;
    i = s.find(kFormatMarker, i + kFormatMarker.size());
  }
  return total;
}

}  // namespace internal

struct DiagnosticDef {
  constexpr DiagnosticDef(std::string_view msg) : msg(msg) {}

  std::string_view msg;
};

// The definition of an error. All instances of ErrorDef are in errors.h.
// Template args define format parameters in the error message.
template <typename... Args>
struct ErrorDef : DiagnosticDef {
  constexpr ErrorDef(std::string_view msg) : DiagnosticDef(msg) {
    // This can't be a static assert because msg is not constexpr.
    assert(sizeof...(Args) == internal::count_format_args(msg) &&
           "number of format string parameters '{}' != number of template arguments");
  }
};

// The definition of a warning. All instances of WarningDef are in errors.h.
// Template args define format parameters in the warning message.
template <typename... Args>
struct WarningDef : DiagnosticDef {
  constexpr WarningDef(std::string_view msg) : DiagnosticDef(msg) {
    // This can't be a static assert because msg is not constexpr.
    assert(sizeof...(Args) == internal::count_format_args(msg) &&
           "number of format string parameters '{}' != number of template arguments");
  }
};

// A tag that indicates whether a diagnostic is an error or warning. In the
// future this could be extended to include hints, suggestions, etc.
enum class DiagnosticKind {
  kError,
  kWarning,
};

// Represents a given instance of an error. Points to the error type it is an
// instance of. Holds a SourceSpan indicating where the error occurred and a
// formatted error message, built from the ErrorDef's message template and
// format parameters passed in to Error's constructor.
// Exists in order to allow deferral of error reporting and to be able to pass
// around errors.
struct Diagnostic {
  Diagnostic(DiagnosticKind kind, const DiagnosticDef& err, const std::optional<SourceSpan>& span,
             const std::string msg)
      : kind(kind), err(err), span(span), msg(msg) {}
  Diagnostic(DiagnosticKind kind, const DiagnosticDef& err, const Token& token,
             const std::string msg)
      : kind(kind), err(err), span(token.span()), msg(msg) {}
  virtual ~Diagnostic() {}

  DiagnosticKind kind;
  const DiagnosticDef& err;
  std::optional<SourceSpan> span;
  std::string msg;
};

template <typename... Args>
struct Error : Diagnostic {
  Error(const ErrorDef<Args...>& err, std::optional<SourceSpan> span, Args... args)
      : Diagnostic(DiagnosticKind::kError, err, span, internal::FormatErr(err.msg, args...)) {}
};

template <typename... Args>
struct Warning : Diagnostic {
  Warning(const WarningDef<Args...>& warn, std::optional<SourceSpan> span, Args... args)
      : Diagnostic(DiagnosticKind::kWarning, warn, span, internal::FormatErr(warn.msg, args...)) {}
};

}  // namespace diagnostics
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTIC_TYPES_H_
