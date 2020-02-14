// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic_parser.h"

#include <cctype>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fxl/logging.h"

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
      case '{':
        ++next_;
        current_lexical_token_ = LexicalToken::kLeftBrace;
        return;
      case '}':
        ++next_;
        current_lexical_token_ = LexicalToken::kRightBrace;
        return;
      case ':':
        ++next_;
        if (*next_ == ':') {
          ++next_;
        } else {
          AddError() << "Missing a second ':'.\n";
        }
        current_lexical_token_ = LexicalToken::kColonColon;
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
        if (!error_found) {
          error_found = true;
          AddError() << "Unknown character <" << *next_ << ">\n";
        }
        ++next_;
        break;
    }
  }
}

void SemanticParser::SkipSemicolon() {
  while (!IsEof()) {
    if (ConsumeSemicolon() || IsRightBrace()) {
      return;
    }
    NextLexicalToken();
  }
}

void SemanticParser::SkipBlock() {
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
  if (IsIdentifier()) {
    Interface* interface = nullptr;
    if (library != nullptr) {
      std::string protocol_name = library->name() + "/" + std::string(current_string_);
      if (!library->GetInterfaceByName(protocol_name, &interface)) {
        AddError() << "Protocol " << current_string_ << " not found in library " << library->name()
                   << '\n';
      }
    }
    NextLexicalToken();
    if (ParseColonColon()) {
      if (IsIdentifier()) {
        InterfaceMethod* method = nullptr;
        if (interface != nullptr) {
          method = interface->GetMethodByName(current_string_);
          if (method == nullptr) {
            AddError() << "Method " << current_string_ << " not found in protocol "
                       << interface->name() << '\n';
          }
        }
        NextLexicalToken();
        if (ParseLeftBrace()) {
          auto method_semantic = std::make_unique<MethodSemantic>();
          while (!ConsumeRightBrace() && !IsEof()) {
            ParseAssignment(method_semantic.get());
          }
          if (method != nullptr) {
            method->set_semantic(std::move(method_semantic));
          }
          ParseRightBrace();
          return;
        }
      } else {
        AddError() << "Method name expected.\n";
      }
    }
  } else {
    AddError() << "Protocol name expected.\n";
  }
  SkipBlock();
  NextLexicalToken();
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
  if (ConsumeSlash()) {
    std::unique_ptr<Expression> right = ParseAccessExpression();
    if (right == nullptr) {
      return nullptr;
    }
    return std::make_unique<ExpressionSlash>(std::move(expression), std::move(right));
  }
  return expression;
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
  if (Consume("request")) {
    return std::make_unique<ExpressionRequest>();
  }
  if (Consume("handle")) {
    return std::make_unique<ExpressionHandle>();
  }
  return nullptr;
}

void SemanticParser::LexerIdentifier() {
  std::string::const_iterator start = next_;
  while (isalnum(*next_) || (*next_ == '_') || ((*next_ == '.') && allow_dots_in_identifiers_)) {
    ++next_;
  }
  current_string_ = std::string_view(&*start, next_ - start);
  current_lexical_token_ = LexicalToken::kIdentifier;
}

}  // namespace semantic
}  // namespace fidl_codec
