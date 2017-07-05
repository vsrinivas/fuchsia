// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <fstream>
#include <string>

#include <magenta/mdi.h>

enum TokenType {
    TOKEN_INVALID = 0,
    TOKEN_EOF,              // returned at end of input
    TOKEN_INT_LITERAL,      // integer literal
    TOKEN_STRING_LITERAL,   // string literal
    TOKEN_IDENTIFIER,
    TOKEN_LIST_START,       // '{'
    TOKEN_LIST_END,         // '{'
    TOKEN_ARRAY_START,      // '['
    TOKEN_ARRAY_END,        // ']'
    TOKEN_EQUALS,           // '='
    TOKEN_COMMA,            // ','
    TOKEN_DOT,              // '.'
    TOKEN_LPAREN,           // '('
    TOKEN_RPAREN,           // ')'

    // unary and binary operators
    TOKEN_PLUS,             // '+'
    TOKEN_MINUS,            // '-'
    TOKEN_TIMES,            // '*'
    TOKEN_DIV,              // '/'
    TOKEN_MOD,              // '%'
    TOKEN_NOT,              // '~'
    TOKEN_AND,              // '&'
    TOKEN_OR,               // '|'
    TOKEN_XOR,              // '^'
    TOKEN_LSHIFT,           // "<<"
    TOKEN_RSHIFT,           // ">>"

    // reserved words
    TOKEN_TRUE,             // "true"
    TOKEN_FALSE,            // "false"
    TOKEN_CONST,            // "const"
    TOKEN_INCLUDE,          // "include"
    TOKEN_UINT8_TYPE,       // "uint8"
    TOKEN_INT32_TYPE,       // "int32"
    TOKEN_UINT32_TYPE,      // "uint32"
    TOKEN_UINT64_TYPE,      // "uint64"
    TOKEN_BOOLEAN_TYPE,     // "boolean"
    TOKEN_STRING_TYPE,      // "string"
    TOKEN_ARRAY_TYPE,       // "array"
    TOKEN_LIST_TYPE,        // "list"
};

struct Token {
    TokenType   type;
    uint64_t    int_value;
    std::string string_value;   // raw string value

    // returns type for type name tokens
    mdi_type_t get_type_name();

    // returns precedence for binary operators,
    // or -1 if token is not a binary operator.
    int get_precedence();

    enum {
        MAX_PRECEDENCE = 1000,
    };

    void print();
};

class Tokenizer {
public:
    Tokenizer();
    ~Tokenizer();

    bool open_file(Tokenizer* container, const char* path);

    // returns false if we cannot parse the next token
    // TOKEN_EOF is returned at end of file
    bool next_token(Token& token);
    bool peek_token(Token& token);

    void print_err(const char* fmt, ...);

private:
    int get_char();
    int next_char();
    int peek_char();
    void eat_whitespace();
    bool parse_identifier(Token& token, int ch);
    bool parse_integer(Token& token, int ch);
    bool parse_string(Token& token);

    std::ifstream in_file;
    std::string current_file;
    std::string current_line;
    int line_number;
    unsigned line_offset;
    int peek[2];

    Token token_peek;
    bool have_token_peek = false;
};
