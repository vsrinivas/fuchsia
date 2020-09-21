// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_PARSER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_PARSER_H_

#include <memory>
#include <optional>

#include "experimental_flags.h"
#include "lexer.h"
#include "raw_ast.h"
#include "reporter.h"
#include "types.h"

namespace fidl {

using namespace diagnostics;
using reporter::Reporter;

// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#_parsing
// for additional context
class Parser {
 public:
  Parser(Lexer* lexer, Reporter* reporter, ExperimentalFlags experimental_flags);

  std::unique_ptr<raw::File> Parse() { return ParseFile(); }

  bool Success() const { return reporter_->errors().size() == 0; }

 private:
  // currently the only usecase for this enum is to identify the case where the parser
  // has seen a doc comment block, followed by a regular comment block, followed by
  // a doc comment block
  enum class State {
    // the parser is currently in a doc comment block
    kDocCommentLast,
    // the parser is currently in a regular comment block, which directly followed a
    // doc comment block
    kDocCommentThenComment,
    // the parser is in kNormal for all other cases
    kNormal,
  };

  Token Lex() {
    for (;;) {
      auto token = lexer_->Lex();
      switch (token.kind()) {
        case Token::Kind::kComment:
          if (state_ == State::kDocCommentLast)
            state_ = State::kDocCommentThenComment;
          break;
        case Token::Kind::kDocComment:
          if (state_ == State::kDocCommentThenComment)
            reporter_->Report(WarnCommentWithinDocCommentBlock, last_token_);
          state_ = State::kDocCommentLast;
          return token;
        default:
          state_ = State::kNormal;
          return token;
      }
    }
  }

  Token::KindAndSubkind Peek() { return last_token_.kind_and_subkind(); }

  // ASTScope is a tool to track the start and end source location of each
  // node automatically.  The parser associates each node with the start and
  // end of its source location.  It also tracks the "gap" in between the
  // start and the previous interesting source element.  As we walk the tree,
  // we create ASTScope objects that can track the beginning and end of the
  // text associated with the Node being built.  The ASTScope object then
  // colludes with the Parser to figure out where the beginning and end of
  // that node are.
  //
  // ASTScope should only be created on the stack, when starting to parse
  // something that will result in a new AST node.
  class ASTScope {
   public:
    explicit ASTScope(Parser* parser) : parser_(parser) {
      suppress_ = parser_->suppress_gap_checks_;
      parser_->suppress_gap_checks_ = false;
      parser_->active_ast_scopes_.push_back(raw::SourceElement(Token(), Token()));
    }
    // The suppress mechanism
    ASTScope(Parser* parser, bool suppress) : parser_(parser), suppress_(suppress) {
      parser_->active_ast_scopes_.push_back(raw::SourceElement(Token(), Token()));
      suppress_ = parser_->suppress_gap_checks_;
      parser_->suppress_gap_checks_ = suppress;
    }
    raw::SourceElement GetSourceElement() {
      parser_->active_ast_scopes_.back().end_ = parser_->previous_token_;
      if (!parser_->suppress_gap_checks_) {
        parser_->last_was_gap_start_ = true;
      }
      return raw::SourceElement(parser_->active_ast_scopes_.back());
    }
    ~ASTScope() {
      parser_->suppress_gap_checks_ = suppress_;
      parser_->active_ast_scopes_.pop_back();
    }

    ASTScope(const ASTScope&) = delete;
    ASTScope& operator=(const ASTScope&) = delete;

   private:
    Parser* parser_;
    bool suppress_;
  };

  void UpdateMarks(Token& token) {
    // There should always be at least one of these - the outermost.
    assert(active_ast_scopes_.size() > 0 && "internal compiler error: unbalanced parse tree");

    if (!suppress_gap_checks_) {
      // If the end of the last node was the start of a gap, record that.
      if (last_was_gap_start_ && previous_token_.kind() != Token::Kind::kNotAToken) {
        gap_start_ = token.previous_end();
        last_was_gap_start_ = false;
      }

      // If this is a start node, then the end of it will be the start of
      // a gap.
      if (active_ast_scopes_.back().start_.kind() == Token::Kind::kNotAToken) {
        last_was_gap_start_ = true;
      }
    }
    // Update the token to record the correct location of the beginning of
    // the gap prior to it.
    if (gap_start_.valid()) {
      token.set_previous_end(gap_start_);
    }

    for (auto& scope : active_ast_scopes_) {
      if (scope.start_.kind() == Token::Kind::kNotAToken) {
        scope.start_ = token;
      }
    }

    previous_token_ = token;
  }

  enum class OnNoMatch {
    kReportAndConsume,  // on failure, report error and return consumed token
    kReportAndRecover,  // on failure, report error and return std::nullopt
    kIgnore,            // on failure, return std::nullopt
  };

  // ReadToken matches on the next token using the predicate |p|, which returns
  // a unique_ptr<Diagnostic> on failure, or nullptr on a match.
  // See #OfKind, and #IdentifierOfSubkind for the two most common predicates.
  // If the predicate doesn't match, ReadToken follows the OnNoMatch enum.
  template <class Predicate>
  std::optional<Token> ReadToken(Predicate p, OnNoMatch on_no_match) {
    std::unique_ptr<Diagnostic> error = p(Peek());
    if (error) {
      switch (on_no_match) {
        case OnNoMatch::kReportAndConsume:
          Fail(std::move(error));
          break;
        case OnNoMatch::kReportAndRecover:
          Fail(std::move(error));
          RecoverOneError();
          return std::nullopt;
        case OnNoMatch::kIgnore:
          return std::nullopt;
      }
    }
    auto token = previous_token_ = last_token_;
    last_token_ = Lex();
    UpdateMarks(token);
    return token;
  }

  // ConsumeToken consumes a token whether or not it matches, and if it doesn't
  // match, it reports an error.
  template <class Predicate>
  std::optional<Token> ConsumeToken(Predicate p) {
    return ReadToken(p, OnNoMatch::kReportAndConsume);
  }

  // ConsumeTokenOrRecover consumes a token if-and-only-if it matches the given
  // predicate |p|. If it doesn't match, it reports an error, then marks that
  // error as recovered, essentially continuing as if the token had been there.
  template <class Predicate>
  std::optional<Token> ConsumeTokenOrRecover(Predicate p) {
    return ReadToken(p, OnNoMatch::kReportAndRecover);
  }

  // MaybeConsumeToken consumes a token if-and-only-if it matches the given
  // predicate |p|.
  template <class Predicate>
  std::optional<Token> MaybeConsumeToken(Predicate p) {
    return ReadToken(p, OnNoMatch::kIgnore);
  }

  static auto OfKind(Token::Kind expected_kind) {
    return [expected_kind](Token::KindAndSubkind actual) -> std::unique_ptr<Diagnostic> {
      if (actual.kind() != expected_kind) {
        return Reporter::MakeError(ErrUnexpectedTokenOfKind, actual,
                                   Token::KindAndSubkind(expected_kind, Token::Subkind::kNone));
      }
      return nullptr;
    };
  }

  static auto IdentifierOfSubkind(Token::Subkind expected_subkind) {
    return [expected_subkind](Token::KindAndSubkind actual) -> std::unique_ptr<Diagnostic> {
      auto expected = Token::KindAndSubkind(Token::Kind::kIdentifier, expected_subkind);
      if (actual.combined() != expected.combined()) {
        return Reporter::MakeError(
            ErrUnexpectedIdentifier, actual,
            Token::KindAndSubkind(Token::Kind::kIdentifier, Token::Subkind::kNone));
      }
      return nullptr;
    };
  }

  bool LookupHandleSubtype(const raw::Identifier* identifier,
                           std::optional<types::HandleSubtype>* out_handle_subtype);

  std::nullptr_t Fail();
  std::nullptr_t Fail(std::unique_ptr<Diagnostic> err);
  template <typename... Args>
  std::nullptr_t Fail(const ErrorDef<Args...>& err, const Args&... args);
  template <typename... Args>
  std::nullptr_t Fail(const ErrorDef<Args...>& err, Token token, const Args&... args);
  template <typename... Args>
  std::nullptr_t Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                      const Args&... args);

  struct Modifiers {
    std::optional<types::Strictness> strictness;
    std::optional<Token> strictness_token;
    std::optional<types::Resourceness> resourceness;
    std::optional<Token> resourceness_token;
  };

  Modifiers ParseModifiers();

  // Reports an error if |modifiers| contains a modifier whose type is not
  // included in |Allowlist|. The |decl_token| should be "struct", "enum", etc.
  // Marks the error as recovered so that parsing will continue.
  template <typename... Allowlist>
  void ValidateModifiers(const Modifiers& modifiers, Token decl_token) {
    const auto fail = [&](std::optional<Token> token) {
      Fail(ErrCannotSpecifyModifier, token.value(), token.value().kind_and_subkind(),
           decl_token.kind_and_subkind());
      RecoverOneError();
    };
    if (!(std::is_same_v<types::Strictness, Allowlist> || ...) && modifiers.strictness) {
      fail(modifiers.strictness_token);
    }
    if (!(std::is_same_v<types::Resourceness, Allowlist> || ...) && modifiers.resourceness) {
      fail(modifiers.resourceness_token);
    }
  }

  std::unique_ptr<raw::Identifier> ParseIdentifier(bool is_discarded = false);
  std::unique_ptr<raw::CompoundIdentifier> ParseCompoundIdentifier();
  std::unique_ptr<raw::CompoundIdentifier> ParseLibraryName();

  std::unique_ptr<raw::StringLiteral> ParseStringLiteral();
  std::unique_ptr<raw::NumericLiteral> ParseNumericLiteral();
  std::unique_ptr<raw::TrueLiteral> ParseTrueLiteral();
  std::unique_ptr<raw::FalseLiteral> ParseFalseLiteral();
  std::unique_ptr<raw::Literal> ParseLiteral();
  std::unique_ptr<raw::Ordinal64> ParseOrdinal64();

  std::unique_ptr<raw::Constant> ParseConstant();

  std::unique_ptr<raw::Attribute> ParseAttribute();
  std::unique_ptr<raw::Attribute> ParseDocComment();
  std::unique_ptr<raw::AttributeList> ParseAttributeList(
      std::unique_ptr<raw::Attribute> doc_comment, ASTScope& scope);
  std::unique_ptr<raw::AttributeList> MaybeParseAttributeList(bool for_parameter = false);

  std::unique_ptr<raw::Using> ParseUsing(std::unique_ptr<raw::AttributeList> attributes, ASTScope&,
                                         const Modifiers&);

  std::unique_ptr<raw::TypeConstructor> ParseTypeConstructor();

  std::unique_ptr<raw::BitsMember> ParseBitsMember();
  std::unique_ptr<raw::BitsDeclaration> ParseBitsDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::ConstDeclaration> ParseConstDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::EnumMember> ParseEnumMember();
  std::unique_ptr<raw::EnumDeclaration> ParseEnumDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::Parameter> ParseParameter();
  std::unique_ptr<raw::ParameterList> ParseParameterList();
  std::unique_ptr<raw::ProtocolMethod> ParseProtocolEvent(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope);
  std::unique_ptr<raw::ProtocolMethod> ParseProtocolMethod(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
      std::unique_ptr<raw::Identifier> method_name);
  // ParseProtocolMember parses any one protocol member, i.e. an event,
  // a method, or a compose stanza.
  void ParseProtocolMember(std::vector<std::unique_ptr<raw::ComposeProtocol>>* composed_protocols,
                           std::vector<std::unique_ptr<raw::ProtocolMethod>>* methods);
  std::unique_ptr<raw::ProtocolDeclaration> ParseProtocolDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::ResourceProperty> ParseResourcePropertyDeclaration();
  std::unique_ptr<raw::ResourceDeclaration> ParseResourceDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::ServiceMember> ParseServiceMember();
  std::unique_ptr<raw::ServiceDeclaration> ParseServiceDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::StructMember> ParseStructMember();
  std::unique_ptr<raw::StructDeclaration> ParseStructDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::TableMember> ParseTableMember();
  std::unique_ptr<raw::TableDeclaration> ParseTableDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::UnionMember> ParseUnionMember();
  std::unique_ptr<raw::UnionDeclaration> ParseUnionDeclaration(
      std::unique_ptr<raw::AttributeList> attributes, ASTScope&, const Modifiers&);

  std::unique_ptr<raw::File> ParseFile();

  enum class RecoverResult {
    Failure,
    Continue,
    EndOfScope,
  };

  // Called when an error is encountered in parsing. Attempts to get the parser
  // back to a valid state, where parsing can continue. Possible results:
  //  * Failure: recovery failed. we are still in an invalid state and cannot
  //    continue.
  //    A signal to `return` a failure from the current parsing function.
  //  * Continue: recovery succeeded. we are in a valid state to continue, at
  //    the same parsing scope as when this was called (e.g. if we just parsed a
  //    decl with an error, we can now parse another decl. If we just parsed a
  //    member of a decl with an error, we can now parse another member.
  //    A signal to `continue` in the current parsing loop.
  //  * EndOfScope: recovery succeeded, but we are now outside the current
  //    parsing scope. For example, we just parsed a decl with an error, and
  //    recovered, but are now at the end of the file.
  //    A signal to `break` out of the current parsing loop.
  RecoverResult RecoverToEndOfDecl();
  RecoverResult RecoverToEndOfMember();
  RecoverResult RecoverToEndOfParam();

  // Utility function used by RecoverTo* methods
  bool ConsumeTokensUntil(std::set<Token::Kind> tokens);

  // Indicates whether we are currently able to continue parsing.
  // Typically when the parser reports an error, it then attempts to recover
  // (get back into a valid state). If this is successful, it updates
  // recovered_errors_ to reflect how many errors are considered "recovered
  // from".
  // Not to be confused with Parser::Success, which is called after parsing to
  // check if any errors were reported during parsing, regardless of recovery.
  bool Ok() const { return reporter_->errors().size() == recovered_errors_; }
  void RecoverOneError() { recovered_errors_++; }
  void RecoverAllErrors() { recovered_errors_ = reporter_->errors().size(); }
  size_t recovered_errors_ = 0;

  std::map<std::string_view, types::HandleSubtype> handle_subtype_table_;

  Lexer* lexer_;
  Reporter* reporter_;
  const ExperimentalFlags experimental_flags_;

  // The stack of information interesting to the currently active ASTScope
  // objects.
  std::vector<raw::SourceElement> active_ast_scopes_;
  // The most recent start of a "gap" - the uninteresting source prior to the
  // beginning of a token (usually mostly containing whitespace).
  SourceSpan gap_start_;
  // Indicates that the last element was the start of a gap, and that the
  // scope should be updated accordingly.
  bool last_was_gap_start_ = false;
  // Suppress updating the gap for the current Scope.  Useful when
  // you don't know whether a scope is going to be interesting lexically, and
  // you have to decide at runtime.
  bool suppress_gap_checks_ = false;
  // The token before last_token_ (below).
  Token previous_token_;

  Token last_token_;
  State state_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_PARSER_H_
