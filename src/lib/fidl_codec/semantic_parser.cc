// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic_parser.h"

#include <lib/syslog/cpp/macros.h>

#include <cctype>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"

namespace fidl_codec {
namespace semantic {

std::ostream& ParserErrors::AddError() {
  ++error_count_;
  return os_;
}

std::ostream& ParserErrors::AddError(const Location& location) {
  ++error_count_;
  // Computes the line and column in the buffer of the error.
  std::string::const_iterator start_line = location.buffer().begin();
  int line = 1;
  int column = 1;
  std::string::const_iterator current = start_line;
  while (current != location.location()) {
    if (*current == '\n') {
      start_line = ++current;
      ++line;
      column = 1;
    } else {
      ++current;
      ++column;
    }
  }
  while (current != location.buffer().end()) {
    if (*current == '\n') {
      break;
    }
    ++current;
  }
  // Displays the line of the error (the whole line).
  os_ << std::string_view(&*start_line, current - start_line) << '\n';
  // Displays some spaces. This way, the caret will be right under the error location.
  // It will point the first character of the previous line where the error is.
  current = start_line;
  for (int i = 1; i < column; ++i) {
    os_ << ((*current == '\t') ? '\t' : ' ');
    ++current;
  }
  // Displays the marker (caret) which points to the error, the line and the column.
  os_ << "^\n" << line << ":" << column << ": ";
  // Returns the stream. This way, the caller can add the error message.
  return os_;
}

void SemanticParser::NextLexicalToken() {
  bool error_found = false;
  for (;;) {
    while (isspace(*next_)) {
      ++next_;
    }
    current_location_ = next_;
    switch (*next_) {
      case '\0':
        current_lexical_token_ = LexicalToken::kEof;
        return;
      case '\'':
        LexerString();
        return;
      case '{':
        ++next_;
        current_lexical_token_ = LexicalToken::kLeftBrace;
        return;
      case '}':
        ++next_;
        current_lexical_token_ = LexicalToken::kRightBrace;
        return;
      case '(':
        ++next_;
        current_lexical_token_ = LexicalToken::kLeftParenthesis;
        return;
      case ')':
        ++next_;
        current_lexical_token_ = LexicalToken::kRightParenthesis;
        return;
      case ':':
        ++next_;
        if (*next_ == ':') {
          ++next_;
          current_lexical_token_ = LexicalToken::kColonColon;
        } else {
          current_lexical_token_ = LexicalToken::kColon;
        }
        return;
      case ',':
        ++next_;
        current_lexical_token_ = LexicalToken::kComma;
        return;
      case '.':
        ++next_;
        current_lexical_token_ = LexicalToken::kDot;
        return;
      case '=':
        ++next_;
        current_lexical_token_ = LexicalToken::kEqual;
        return;
      case ';':
        ++next_;
        current_lexical_token_ = LexicalToken::kSemicolon;
        return;
      case '/':
        ++next_;
        current_lexical_token_ = LexicalToken::kSlash;
        return;
      default:
        if (isalpha(*next_) || (*next_ == '_')) {
          LexerIdentifier();
          return;
        }
        if (!error_found && !ignore_unknown_characters_) {
          error_found = true;
          AddError() << "Unknown character <" << *next_ << ">\n";
        }
        ++next_;
        break;
    }
  }
}

void SemanticParser::JumpToSemicolon() {
  IgnoreUnknownCharacters ignore_unknown_characters(this);
  while (!IsEof()) {
    if (IsSemicolon() || IsRightBrace()) {
      return;
    }
    if (ConsumeLeftParenthesis()) {
      SkipRightParenthesis();
    } else {
      NextLexicalToken();
    }
  }
}

void SemanticParser::SkipSemicolon() {
  IgnoreUnknownCharacters ignore_unknown_characters(this);
  while (!IsEof()) {
    if (ConsumeSemicolon() || IsRightBrace()) {
      return;
    }
    if (ConsumeLeftParenthesis()) {
      SkipRightParenthesis();
    } else {
      NextLexicalToken();
    }
  }
}

void SemanticParser::SkipBlock() {
  IgnoreUnknownCharacters ignore_unknown_characters(this);
  while (!IsEof()) {
    if (ConsumeRightBrace() || ConsumeSemicolon()) {
      return;
    }
    if (ConsumeLeftBrace()) {
      SkipRightBrace();
    } else {
      NextLexicalToken();
    }
  }
}

void SemanticParser::SkipRightBrace() {
  IgnoreUnknownCharacters ignore_unknown_characters(this);
  while (!IsEof()) {
    if (ConsumeRightBrace()) {
      return;
    }
    if (ConsumeLeftBrace()) {
      SkipRightBrace();
    } else {
      NextLexicalToken();
    }
  }
}

void SemanticParser::SkipRightParenthesis() {
  IgnoreUnknownCharacters ignore_unknown_characters(this);
  while (!IsEof()) {
    if (ConsumeRightParenthesis() || IsSemicolon()) {
      return;
    }
    if (ConsumeLeftBrace()) {
      SkipRightBrace();
    } else if (ConsumeLeftParenthesis()) {
      SkipRightParenthesis();
    } else {
      NextLexicalToken();
    }
  }
}

std::string SemanticParser::ConsumeString() {
  std::string result = std::string(current_string_);
  size_t pos = 0;
  for (;;) {
    auto backslash = result.find('\\', pos);
    if (backslash == std::string::npos) {
      NextLexicalToken();
      return result;
    }
    // We already checked that backslashes are followed by another character.
    FX_DCHECK(backslash < result.size() - 1);
    result.erase(backslash);
    pos = backslash + 1;
  }
}

void SemanticParser::ParseSemantic() {
  while (!IsEof()) {
    if (Is("library")) {
      ParseLibrary();
    } else {
      AddError() << "Keyword 'library' expected.\n";
      SkipBlock();
    }
  }
}

void SemanticParser::ParseLibrary() {
  allow_dots_in_identifiers_ = true;
  NextLexicalToken();
  if (!IsIdentifier()) {
    AddError() << "Library name expected.\n";
    SkipBlock();
    return;
  }
  allow_dots_in_identifiers_ = false;
  Library* library = library_loader_->GetLibraryFromName(std::string(current_string_));
  if (library == nullptr) {
    AddError() << "Library " << current_string_ << " not found.\n";
  } else {
    library->DecodeTypes();
  }
  NextLexicalToken();
  if (!ParseLeftBrace()) {
    SkipBlock();
    return;
  }
  while (!ConsumeRightBrace()) {
    if (!IsIdentifier()) {
      AddError() << "Protocol name expected.\n";
      SkipBlock();
      NextLexicalToken();
      return;
    }
    Interface* interface = nullptr;
    if (library != nullptr) {
      std::string protocol_name = library->name() + "/" + std::string(current_string_);
      if (!library->GetInterfaceByName(protocol_name, &interface)) {
        AddError() << "Protocol " << current_string_ << " not found in library " << library->name()
                   << '\n';
      }
    }
    NextLexicalToken();
    if (!ParseColonColon()) {
      SkipBlock();
      NextLexicalToken();
      return;
    }
    if (!IsIdentifier()) {
      AddError() << "Method name expected.\n";
      SkipBlock();
      NextLexicalToken();
      return;
    }
    InterfaceMethod* method = nullptr;
    if (interface != nullptr) {
      method = interface->GetMethodByName(current_string_);
      if (method == nullptr) {
        AddError() << "Method " << current_string_ << " not found in protocol " << interface->name()
                   << '\n';
      }
    }
    NextLexicalToken();
    if (!ParseLeftBrace()) {
      SkipBlock();
      NextLexicalToken();
      return;
    }
    auto method_semantic = std::make_unique<MethodSemantic>();
    while (!ConsumeRightBrace() && !IsEof()) {
      ParseAssignment(method_semantic.get());
    }
    if (method != nullptr) {
      method->set_semantic(std::move(method_semantic));
    }
  }
}

void SemanticParser::ParseAssignment(MethodSemantic* method_semantic) {
  std::unique_ptr<Expression> destination = ParseExpression();
  if (destination == nullptr) {
    AddError() << "Assignment expected.\n";
    SkipSemicolon();
    return;
  }
  if (!ParseEqual()) {
    SkipSemicolon();
    return;
  }
  std::unique_ptr<Expression> source = ParseExpression();
  if (source == nullptr) {
    AddError() << "Expression expected.\n";
    SkipSemicolon();
    return;
  }
  method_semantic->AddAssignment(std::move(destination), std::move(source));
  if (!ParseSemicolon()) {
    SkipSemicolon();
  }
}

std::unique_ptr<Expression> SemanticParser::ParseExpression() {
  return ParseMultiplicativeExpression();
}

std::unique_ptr<Expression> SemanticParser::ParseMultiplicativeExpression() {
  std::unique_ptr<Expression> expression = ParseAccessExpression();
  if (expression == nullptr) {
    return nullptr;
  }
  for (;;) {
    if (ConsumeSlash()) {
      std::unique_ptr<Expression> right = ParseAccessExpression();
      if (right == nullptr) {
        return nullptr;
      }
      expression = std::make_unique<ExpressionSlash>(std::move(expression), std::move(right));
    } else if (ConsumeColon()) {
      std::unique_ptr<Expression> right = ParseAccessExpression();
      if (right == nullptr) {
        return nullptr;
      }
      expression = std::make_unique<ExpressionColon>(std::move(expression), std::move(right));
    } else {
      return expression;
    }
  }
}

std::unique_ptr<Expression> SemanticParser::ParseAccessExpression() {
  std::unique_ptr<Expression> expression = ParseTerminalExpression();
  if (expression == nullptr) {
    return nullptr;
  }
  for (;;) {
    if (ConsumeDot()) {
      if (!IsIdentifier()) {
        AddError() << "Field name expected.\n";
        expression = std::make_unique<ExpressionFieldAccess>(std::move(expression), "");
      } else {
        std::string_view name = current_string_;
        NextLexicalToken();
        expression = std::make_unique<ExpressionFieldAccess>(std::move(expression), name);
      }
    } else {
      return expression;
    }
  }
}

std::unique_ptr<Expression> SemanticParser::ParseTerminalExpression() {
  if (IsString()) {
    return std::make_unique<ExpressionStringLiteral>(ConsumeString());
  }
  if (Consume("request")) {
    return std::make_unique<ExpressionRequest>();
  }
  if (Consume("handle")) {
    return std::make_unique<ExpressionHandle>();
  }
  if (Consume("HandleDescription")) {
    return ParseHandleDescription();
  }
  return nullptr;
}

std::unique_ptr<Expression> SemanticParser::ParseHandleDescription() {
  if (!ParseLeftParenthesis()) {
    JumpToSemicolon();
    return std::make_unique<ExpressionHandleDescription>(nullptr, nullptr);
  }
  std::unique_ptr<Expression> type = ParseExpression();
  if (type == nullptr) {
    AddError() << "Expression expected (handle type)";
    SkipRightParenthesis();
    return std::make_unique<ExpressionHandleDescription>(nullptr, nullptr);
  }
  if (!ParseComma()) {
    SkipRightParenthesis();
    return std::make_unique<ExpressionHandleDescription>(std::move(type), nullptr);
  }
  std::unique_ptr<Expression> path = ParseExpression();
  if (path == nullptr) {
    AddError() << "Expression expected (handle path)";
    SkipRightParenthesis();
    return std::make_unique<ExpressionHandleDescription>(std::move(type), nullptr);
  }
  if (!ParseRightParenthesis()) {
    SkipRightParenthesis();
  }
  return std::make_unique<ExpressionHandleDescription>(std::move(type), std::move(path));
}

void SemanticParser::LexerIdentifier() {
  std::string::const_iterator start = next_;
  while (isalnum(*next_) || (*next_ == '_') || ((*next_ == '.') && allow_dots_in_identifiers_)) {
    ++next_;
  }
  current_string_ = std::string_view(&*start, next_ - start);
  current_lexical_token_ = LexicalToken::kIdentifier;
}

void SemanticParser::LexerString() {
  std::string::const_iterator start = ++next_;
  while (*next_ != '\'') {
    if (*next_ == '\0') {
      AddError() << "Unterminated string.\n";
      current_lexical_token_ = LexicalToken::kString;
      next_ = start;
      current_string_ = std::string_view(&*start, 0);
      return;
    }
    if (*next_ == '\\') {
      ++next_;
      if (*next_ == '\0') {
        AddError() << "Unterminated string.\n";
        current_lexical_token_ = LexicalToken::kString;
        next_ = start;
        current_string_ = std::string_view(&*start, 0);
        return;
      }
    }
    ++next_;
  }
  current_string_ = std::string_view(&*start, next_ - start);
  ++next_;
  current_lexical_token_ = LexicalToken::kString;
}

}  // namespace semantic
}  // namespace fidl_codec
