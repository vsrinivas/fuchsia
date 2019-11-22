// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_PARSER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_PARSER_H_

#include <memory>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/name_lookup.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

namespace zxdb {

class ExprParser {
 public:
  // The name lookup callback can be empty if the caller doesn't have any symbol context. This means
  // that we can't disambiguate some cases like how to parse "Foo < 1 > bar". In this mode, we'll
  // assume that "<" after a name always mean a template rather than a comparison operation.
  ExprParser(std::vector<ExprToken> tokens, ExprLanguage lang,
             NameLookupCallback name_lookup = NameLookupCallback());

  // Returns the root expression node on successful parsing. On error, returns an empty pointer in
  // which case the error message can be read from err() ad error_token()
  fxl::RefPtr<ExprNode> Parse();

  // Attempts to parse the given string as an identifier. The returned err indicates whether the
  // output identifier is valid.
  static Err ParseIdentifier(const std::string& input, Identifier* output);
  static Err ParseIdentifier(const std::string& input, ParsedIdentifier* output);

  // The result of parsing. Since this does not have access to the initial string, it will not
  // indicate context for the error. That can be generated from the error_token() if desired.
  const Err& err() const { return err_; }

  ExprToken error_token() const { return error_token_; }

 private:
  struct DispatchInfo;

  // When recursively calling this function, call with the same precedence as the current expression
  // for left-associativity (operators evaluated from left-to-right), and one less for
  // right-associativity.
  fxl::RefPtr<ExprNode> ParseExpression(int precedence);

  // Parses the name of a symbol or a non-type identifier (e.g. a variable name) starting at
  // cur_token().
  //
  // This is separate from the regular parsing to simplify the structure. These names can be parsed
  // linearly (we don't go into templates which is where recursion comes in) so the implementation
  // is more straightforward, and it's nicer to get the handling out of the general "<" token
  // handler, for example.
  //
  // On error, has_error() will be set and an empty ParseNameResult will be returned (with empty
  // identifier and type).
  //
  // The |expand_types| flag indicates if ParseName() should call ParseType() when it identifies a
  // type name identifier. This will then handle following type modifiers like "*" and "&&".
  // External callers will want to set this. This flag is set to false when called by ParseType() to
  // avoid recursive calls.
  struct ParseNameResult {
    ParseNameResult() = default;

    // On success, always contains the identifier name.
    ParsedIdentifier ident;

    // When the result is a type, this will contain the resolved type. When null, the result is a
    // non-type or an error.
    fxl::RefPtr<Type> type;
  };
  ParseNameResult ParseName(bool expand_types);

  // Parses a type starting at cur_token() and returns it. Returns a null type and sets has_error()
  // on failure.
  //
  // If the optional_base is empty, the whole type will be parsed. Examples of things that it will
  // handle in this case: "const Foo *", "Bar &", "int".
  //
  // This may be called by ParseName when it realizes that it just generated a type. This is
  // necessary to handle stuff like "*" and "&&" that follow the type name and modify it. In this
  // case, the optional_base would be the type name corresponding to the identifier (e.g.
  // "my_ns::MyClass") and the tokens at cur_token() might be "* *" or "&&" or something that's not
  // a valid type modifier at all (which will mark the type parsing complete).
  fxl::RefPtr<Type> ParseType(fxl::RefPtr<Type> optional_base);

  // Parse a Rust Array type name, which is of the form [Type] or [Type; 24]
  fxl::RefPtr<Type> ParseRustArrayType();

  // Parses template parameter lists. The "stop_before" parameter indicates how the list is expected
  // to end (i.e. ">"). Sets the error on failure.
  std::vector<std::string> ParseTemplateList(ExprTokenType stop_before);

  // Parses comma-separated lists of expressions. Runs until the given ending token is found
  // (normally a ')' for a function call).
  std::vector<fxl::RefPtr<ExprNode>> ParseExpressionList(ExprTokenType stop_before);

  // These handlers will be passed a token that was just consumed, so the current state of the
  // parser will point to the *next* token (or the end).
  fxl::RefPtr<ExprNode> AmpersandPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> BinaryOpInfix(fxl::RefPtr<ExprNode> left, const ExprToken& token);
  fxl::RefPtr<ExprNode> DotOrArrowInfix(fxl::RefPtr<ExprNode> left, const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftParenPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftParenInfix(fxl::RefPtr<ExprNode> left, const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftSquareInfix(fxl::RefPtr<ExprNode> left, const ExprToken& token);
  fxl::RefPtr<ExprNode> RustCastInfix(fxl::RefPtr<ExprNode> left, const ExprToken& token);
  fxl::RefPtr<ExprNode> LiteralPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> UnaryPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> NamePrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> StarPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> CastPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> SizeofPrefix(const ExprToken& token);

  // Returns true if the next token is the given type.
  bool LookAhead(ExprTokenType type) const;

  // Returns the next token or the invalid token if nothing is left. Advances to the next token.
  const ExprToken& Consume();

  // Consumes a token of the given type, returning it if there was one available and the type
  // matches. Otherwise, sets the error condition using the given message and returns a reference to
  // an invalid token.
  //
  // If the error_token is provided (it's not kInvalid type) it will be used to blame the error on.
  // Otherwise, the next token checked will be blamed.
  const ExprToken& Consume(ExprTokenType type, const char* error_msg,
                           const ExprToken& error_token = ExprToken());

  // Like Consume() but remaps two adjacent ">" tokens to a shift right ">>" operator. This is used
  // in cases where you need to handle shifts and know that it's not part of an identifier. See
  // IsCurTokenShiftRight().
  ExprToken ConsumeWithShiftTokenConversion();

  // Reads a sequence of cv-qualifiers (plus "restrict" for C) and appends to the vector in order.
  // Only matching tokens are consumed, it stops consuming at the next non-qualifier.
  //
  // Duplicate qualifications will trigger errors (has_error() will be set). The input is *not*
  // reset so this can be used to add qualifiers to an existing set while also triggering errors for
  // duplicates for the additions.
  void ConsumeCVQualifier(std::vector<DwarfTag>* qual);

  // Applies the given type modifier tags to the given input in order and returns the newly
  // qualified type.
  fxl::RefPtr<Type> ApplyQualifiers(fxl::RefPtr<Type> input, const std::vector<DwarfTag>& qual);

  void SetError(const ExprToken& token, std::string msg);

  // Returns true if the current token is the first of a pair of adjacent ">" tokens that might
  // compose a shift right token (">>"). Because of ambiguity, the tokenizer always tokenizes these
  // separately and we have to decide based on context what it is.
  bool IsCurTokenShiftRight() const;

  // Equivalent to cur_token().precedence except this remaps two adjacent ">" to a ">>" precedence.
  // See IsCurTokenShiftRight().
  int CurPrecedenceWithShiftTokenConversion() const;

  // Call this only if !at_end().
  const ExprToken& cur_token() const { return tokens_[cur_]; }

  bool has_error() const { return err_.has_error(); }
  bool at_end() const { return cur_ == tokens_.size(); }

  static const DispatchInfo& DispatchForToken(const ExprToken& token);
  static DispatchInfo kDispatchInfo[];

  ExprLanguage language_;

  // Possibly null, see constructor.
  NameLookupCallback name_lookup_callback_;

  std::vector<ExprToken> tokens_;
  size_t cur_ = 0;  // Current index into tokens_.

  // On error, the message and token where an error was encountered.
  Err err_;
  ExprToken error_token_;

  // This is a kInvalid token that we can return in error cases without having to reference
  // something in the tokens_ array.
  static const ExprToken kInvalidToken;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_PARSER_H_
