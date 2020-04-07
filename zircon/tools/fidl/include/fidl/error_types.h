// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_TYPES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_TYPES_H_

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

constexpr int count_format_args(std::string_view s, size_t i = 0) {
  if (i + 1 >= s.size()) {
    return 0;
  }
  int extra = 0;
  if (s[i] == '{' && s[i + 1] == '}') {
    extra = 1;
  }
  return extra + count_format_args(s, i + 1);
}

struct BaseError {
  std::string_view msg;

  constexpr BaseError(std::string_view msg) : msg(msg) {}
};

// The definition of an error. All instances of Error are in this header.
// Template args define format parameters in the error message.
template <typename... Args>
struct Error : BaseError {
  constexpr Error(std::string_view msg) : BaseError(msg) {
    assert(sizeof...(Args) == count_format_args(msg) &&
           "number of format string parameters '{}' != number of template arguments");
  }
};

namespace internal {
  constexpr std::string_view kFormatMarker = "{}";

  std::string Display(const std::string& s);
  std::string Display(const std::set<std::string>& s);
  std::string Display(const SourceSpan& s);
  std::string Display(const Token::KindAndSubkind& t);
  std::string Display(const raw::Attribute& a);
  std::string Display(raw::AttributeList* a);
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
}  // namespace internal

struct BaseReportedError {
  const BaseError* err;
  std::optional<SourceSpan> span;

  BaseReportedError(const BaseError* err, const std::optional<SourceSpan>& span)
    : err(err), span(span) {}
  BaseReportedError(const BaseError* err, const Token& token)
    : err(err), span(token.span()) {}
  virtual ~BaseReportedError() {}

  virtual std::string Format() const = 0;
};

// Represents a given instance of an error. Points to the error type it is an
// instance of. Holds values of format parameters as a tuple in order to defer
// formatting/reporting and be able to pass around errors.
template <typename... Args>
struct ReportedError : BaseReportedError {
  std::tuple<Args...> params;

  ReportedError(const Error<Args...>* err, std::optional<SourceSpan> span, Args... args)
    : BaseReportedError(err, span), params(std::make_tuple(args...)) {}

  template<std::size_t... Is>
  std::string call_format_err(std::index_sequence<Is...>) const {
    return internal::FormatErr(err->msg, std::get<Is>(params)...);
  }

  std::string Format() const override {
    return call_format_err(std::index_sequence_for<Args...>());
  }
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_TYPES_H_
