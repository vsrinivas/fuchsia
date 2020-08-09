// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_H_
#define SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_H_

#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include "src/lib/fidl_codec/semantic.h"

namespace fidl_codec {

class LibraryLoader;

namespace semantic {

// Defines a location within a buffer.
class Location {
 public:
  Location(const std::string& buffer, std::string::const_iterator location)
      : buffer_(buffer), location_(location) {}

  const std::string& buffer() const { return buffer_; }
  std::string::const_iterator location() const { return location_; }

 private:
  // Reference to the buffer which contains the text we are parsing.
  const std::string& buffer_;
  // Location within this buffer.
  const std::string::const_iterator location_;
};

// Handles the parser errors.
class ParserErrors {
 public:
  explicit ParserErrors(std::ostream& os = std::cerr) : os_(os) {}

  int error_count() const { return error_count_; }
  bool has_error() const { return error_count_ > 0; }

  // Adds a global error (not associated to a location in the buffer).
  std::ostream& AddError();

  // Adds an error at the specified location.
  std::ostream& AddError(const Location& location);

 private:
  // The stream which receives the errors.
  std::ostream& os_;
  // The number of errors which have been generated.
  int error_count_ = 0;
};

// All the lexical tokens we can reduce.
enum class LexicalToken {
  // The end of the file has been found.
  kEof,
  // An identifier. If allow_dots_in_indentifiers is true, an identifier can contain dots.
  kIdentifier,
  // A string (delimited by single quotes).
  kString,
  // A left brace: {
  kLeftBrace,
  // A right brace: }
  kRightBrace,
  // A left parenthesis: (
  kLeftParenthesis,
  // A right parenthesis: )
  kRightParenthesis,
  // One colon: :
  kColon,
  // Two colons: ::
  kColonColon,
  // A comma: ,
  kComma,
  // A dot: .
  kDot,
  // The equal sign: =
  kEqual,
  // A semicolon: ;
  kSemicolon,
  // A slash: /
  kSlash
};

// Parser for the language which defines semantic rules for FIDL methods.
class SemanticParser {
 public:
  SemanticParser(LibraryLoader* library_loader, const std::string& buffer, ParserErrors* errors)
      : library_loader_(library_loader), buffer_(buffer), errors_(errors) {
    next_ = buffer_.begin();
    NextLexicalToken();
  }

  // Returns the location of the current lexical token.
  Location GetLocation() const { return Location(buffer_, current_location_); }

  // Adds an error at the current lexical token location.
  std::ostream& AddError() { return errors_->AddError(GetLocation()); }

  // Reduce the next lexical token. The parser always has a current not used yet lexical token
  // reduced by NextLexicalToken.
  void NextLexicalToken();

  // Skips text until a semicolon or a right brace are found. If a semicolon or a right brace are
  // found, the parsing continues before the semicolon or the right brace.
  void JumpToSemicolon();
  // Skips text until a semicolon or a right brace are found. If a semicolon is found, the parsing
  // continues after the semicolon. If a right brace is found, the parsing continues before the
  // right brace.
  void SkipSemicolon();
  // Skips text until a semicolon or a right brace are found. The parsing continue after the
  // semicolon or the right brace. If an embeded block is found (delimited by left and right
  // braces), the block is skipped.
  void SkipBlock();
  // Skips text until a right brace is found. The parsing continue after the right brace. If an
  // embeded block is found (delimited by left and right braces), the block is skipped.
  void SkipRightBrace();
  // Skips text until a right parenthesis is found. The parsing continue after the right
  // parenthesis. If an embeded block is found (delimited by left and right braces or left and right
  // parentheses), the block is skipped. If a semicolon is found, the parsing resumes before the
  // semicolon.
  void SkipRightParenthesis();

  // Helpers to check that we currently have a specified lexical token.
  bool Is(std::string_view keyword) { return IsIdentifier() && (current_string_ == keyword); }
  bool IsEof() const { return current_lexical_token_ == LexicalToken::kEof; }
  bool IsIdentifier() const { return current_lexical_token_ == LexicalToken::kIdentifier; }
  bool IsString() const { return current_lexical_token_ == LexicalToken::kString; }
  bool IsLeftBrace() const { return current_lexical_token_ == LexicalToken::kLeftBrace; }
  bool IsRightBrace() const { return current_lexical_token_ == LexicalToken::kRightBrace; }
  bool IsRightParenthesis() const {
    return current_lexical_token_ == LexicalToken::kRightParenthesis;
  }
  bool IsColonColon() const { return current_lexical_token_ == LexicalToken::kColonColon; }
  bool IsDot() const { return current_lexical_token_ == LexicalToken::kDot; }
  bool IsEqual() const { return current_lexical_token_ == LexicalToken::kEqual; }
  bool IsSemicolon() const { return current_lexical_token_ == LexicalToken::kSemicolon; }
  bool IsSlash() const { return current_lexical_token_ == LexicalToken::kSlash; }

  // Helpers to check and consume a specified lexical token.
  bool Consume(std::string_view keyword) {
    bool result = IsIdentifier() && (current_string_ == keyword);
    if (result) {
      NextLexicalToken();
    }
    return result;
  }
  bool Consume(LexicalToken token) {
    bool result = current_lexical_token_ == token;
    if (result) {
      NextLexicalToken();
    }
    return result;
  }
  bool ConsumeLeftBrace() { return Consume(LexicalToken::kLeftBrace); }
  bool ConsumeRightBrace() { return Consume(LexicalToken::kRightBrace); }
  bool ConsumeLeftParenthesis() { return Consume(LexicalToken::kLeftParenthesis); }
  bool ConsumeRightParenthesis() { return Consume(LexicalToken::kRightParenthesis); }
  bool ConsumeColon() { return Consume(LexicalToken::kColon); }
  bool ConsumeDot() { return Consume(LexicalToken::kDot); }
  bool ConsumeEqual() { return Consume(LexicalToken::kEqual); }
  bool ConsumeSemicolon() { return Consume(LexicalToken::kSemicolon); }
  bool ConsumeSlash() { return Consume(LexicalToken::kSlash); }

  // Helpers to check and consume a specified lexical token. If the token is not found, an error is
  // generated.
  bool Parse(std::string_view keyword) {
    bool result = IsIdentifier() && (current_string_ == keyword);
    if (result) {
      NextLexicalToken();
    } else {
      AddError() << "Keyword '" << keyword << "' expected.\n";
    }
    return result;
  }
  bool Parse(LexicalToken token, std::string_view token_string) {
    bool result = current_lexical_token_ == token;
    if (result) {
      NextLexicalToken();
    } else {
      AddError() << "Symbol '" << token_string << "' expected.\n";
    }
    return result;
  }
  bool ParseLeftBrace() { return Parse(LexicalToken::kLeftBrace, "{"); }
  bool ParseRightBrace() { return Parse(LexicalToken::kRightBrace, "}"); }
  bool ParseLeftParenthesis() { return Parse(LexicalToken::kLeftParenthesis, "("); }
  bool ParseRightParenthesis() { return Parse(LexicalToken::kRightParenthesis, ")"); }
  bool ParseColonColon() { return Parse(LexicalToken::kColonColon, "::"); }
  bool ParseComma() { return Parse(LexicalToken::kComma, ","); }
  bool ParseEqual() { return Parse(LexicalToken::kEqual, "="); }
  bool ParseSemicolon() { return Parse(LexicalToken::kSemicolon, ";"); }

  // Returns the current string. Escaped characters are resolved.
  // Then it advances to the next lexical item.
  std::string ConsumeString();

  // Parses a file which contains handle semantic rules.
  void ParseSemantic();
  // Parses a library block.
  void ParseLibrary();
  // Parses an assignment (that is a semantic rule).
  void ParseAssignment(MethodSemantic* method_semantic);
  // Parses an expression.
  std::unique_ptr<Expression> ParseExpression();
  // Parses a multiplicative expression (a factor).
  std::unique_ptr<Expression> ParseMultiplicativeExpression();
  // Parses a field access expression.
  std::unique_ptr<Expression> ParseAccessExpression();
  // Parses terminal expressions.
  std::unique_ptr<Expression> ParseTerminalExpression();
  // Parses a handle description: HandleDescription(type, path).
  std::unique_ptr<Expression> ParseHandleDescription();

 private:
  // Lexical reduction of an identifier.
  void LexerIdentifier();

  // Lexical reduction of a string.
  void LexerString();

  // The library loader for which we are parsing the semantic rules. The field semantic from
  // protocol methods is assigned when a rule is parsed.
  LibraryLoader* const library_loader_;
  // The text buffer we are currently parsing.
  const std::string& buffer_;
  // The error handling object.
  ParserErrors* errors_;
  // Location in the buffer of the last lexical token reduced by NextLexicalToken.
  std::string::const_iterator current_location_;
  // Next location in the buffer which will be analyzed by NextLexicalToken.
  std::string::const_iterator next_;
  // Last lexical token reduced by NextLexicalToken.
  LexicalToken current_lexical_token_ = LexicalToken::kEof;
  // For LexicalToken::kIdentifier, the value of the identifier.
  std::string_view current_string_;
  // When this field is true, LexerIdentifier accepts dots within the identifiers. This is used to
  // be able to parse library names like "fuchsia.shell".
  bool allow_dots_in_identifiers_ = false;
  // True when we are doing error recovery to ignore unknown characters.
  bool ignore_unknown_characters_ = false;

  // Used to define a scope for which unknown characters are ignored.
  class IgnoreUnknownCharacters {
   public:
    IgnoreUnknownCharacters(SemanticParser* parser)
        : parser_(parser), saved_value_(parser_->ignore_unknown_characters_) {
      parser->ignore_unknown_characters_ = true;
    }
    ~IgnoreUnknownCharacters() { parser_->ignore_unknown_characters_ = saved_value_; }

   private:
    SemanticParser* const parser_;
    const bool saved_value_;
  };
};

}  // namespace semantic
}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_SEMANTIC_PARSER_H_
