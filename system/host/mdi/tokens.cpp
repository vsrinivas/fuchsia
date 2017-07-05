// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "tokens.h"

//#define PRINT_TOKENS 1

struct ReservedWord {
    TokenType   token;
    const char* word;
} reserved_words [] = {
    { TOKEN_TRUE,           "true" },
    { TOKEN_FALSE,          "false" },
    { TOKEN_CONST,          "const" },
    { TOKEN_INCLUDE,        "include" },
    { TOKEN_UINT8_TYPE,     "uint8" },
    { TOKEN_INT32_TYPE,     "int32" },
    { TOKEN_UINT32_TYPE,    "uint32" },
    { TOKEN_UINT64_TYPE,    "uint64" },
    { TOKEN_BOOLEAN_TYPE,   "boolean" },
    { TOKEN_STRING_TYPE,    "string" },
    { TOKEN_ARRAY_TYPE,     "array" },
    { TOKEN_LIST_TYPE,      "list" },
    { TOKEN_INVALID,        nullptr },
};

TokenType find_reserved_word(std::string& string) {
    const char* str = string.c_str();
    ReservedWord* test = reserved_words;

    while (test->word) {
        if (!strcmp(str, test->word)) {
            return test->token;
        }
        test++;
    }
    return TOKEN_IDENTIFIER;
}

mdi_type_t Token::get_type_name() {
    switch (type) {
    case TOKEN_UINT8_TYPE:
        return MDI_UINT8;
    case TOKEN_INT32_TYPE:
        return MDI_INT32;
    case TOKEN_UINT32_TYPE:
        return MDI_UINT32;
    case TOKEN_UINT64_TYPE:
        return MDI_UINT64;
    case TOKEN_BOOLEAN_TYPE:
        return MDI_BOOLEAN;
    case TOKEN_STRING_TYPE:
        return MDI_STRING;
    case TOKEN_ARRAY_TYPE:
        return MDI_ARRAY;
    case TOKEN_LIST_TYPE:
        return MDI_LIST;
    default:
        return MDI_INVALID_TYPE;
    }
}

// returns precedence for binary operators
int Token::get_precedence() {
    switch (type) {
    case TOKEN_OR:
        return 1;
    case TOKEN_XOR:
        return 2;
    case TOKEN_AND:
        return 3;
    case TOKEN_LSHIFT:
    case TOKEN_RSHIFT:
        return 4;
    case TOKEN_PLUS:
    case TOKEN_MINUS:
        return 5;
    case TOKEN_TIMES:
    case TOKEN_DIV:
    case TOKEN_MOD:
        return 6;
    default:
        // not a binary operator
        return -1;
    }
}

void Token::print() {
    switch (type) {
    case TOKEN_INVALID:
        printf("TOKEN_INVALID\n");
        break;
    case TOKEN_EOF:
        printf("TOKEN_EOF\n");
        break;
    case TOKEN_INT_LITERAL:
        printf("TOKEN_INT_LITERAL %" PRId64 "\n", int_value);
        break;
    case TOKEN_STRING_LITERAL:
        printf("TOKEN_STRING_LITERAL %s\n", string_value.c_str());
        break;
    case TOKEN_IDENTIFIER:
        printf("TOKEN_IDENTIFIER %s\n", string_value.c_str());
        break;
    case TOKEN_LIST_START:
        printf("TOKEN_LIST_START\n");
        break;
    case TOKEN_LIST_END:
        printf("TOKEN_LIST_END\n");
        break;
    case TOKEN_ARRAY_START:
        printf("TOKEN_ARRAY_START\n");
        break;
    case TOKEN_ARRAY_END:
        printf("TOKEN_ARRAY_END\n");
        break;
    case TOKEN_EQUALS:
        printf("TOKEN_EQUALS\n");
        break;
    case TOKEN_COMMA:
        printf("TOKEN_COMMA\n");
        break;
    case TOKEN_DOT:
        printf("TOKEN_DOT\n");
        break;
    case TOKEN_LPAREN:
        printf("TOKEN_LPAREN\n");
        break;
    case TOKEN_RPAREN:
        printf("TOKEN_RPAREN\n");
        break;
    case TOKEN_PLUS:
        printf("TOKEN_PLUS\n");
        break;
    case TOKEN_MINUS:
        printf("TOKEN_MINUS\n");
        break;
    case TOKEN_TIMES:
        printf("TOKEN_TIMES\n");
        break;
    case TOKEN_DIV:
        printf("TOKEN_DIV\n");
        break;
    case TOKEN_MOD:
        printf("TOKEN_MOD\n");
        break;
    case TOKEN_NOT:
        printf("TOKEN_NOT\n");
        break;
    case TOKEN_AND:
        printf("TOKEN_AND\n");
        break;
    case TOKEN_OR:
        printf("TOKEN_OR\n");
        break;
    case TOKEN_XOR:
        printf("TOKEN_XOR\n");
        break;
    case TOKEN_LSHIFT:
        printf("TOKEN_LSHIFT\n");
        break;
    case TOKEN_RSHIFT:
        printf("TOKEN_RSHIFT\n");
        break;
    case TOKEN_TRUE:
        printf("TOKEN_TRUE\n");
        break;
    case TOKEN_FALSE:
        printf("TOKEN_FALSE\n");
        break;
    case TOKEN_CONST:
        printf("TOKEN_CONST\n");
        break;
    case TOKEN_INCLUDE:
        printf("TOKEN_INCLUDE\n");
        break;
    case TOKEN_UINT8_TYPE:
        printf("TOKEN_UINT8_TYPE\n");
        break;
    case TOKEN_INT32_TYPE:
        printf("TOKEN_INT32_TYPE\n");
        break;
    case TOKEN_UINT32_TYPE:
        printf("TOKEN_UINT32_TYPE\n");
        break;
    case TOKEN_UINT64_TYPE:
        printf("TOKEN_UINT64_TYPE\n");
        break;
    case TOKEN_BOOLEAN_TYPE:
        printf("TOKEN_BOOLEAN_TYPE\n");
        break;
    case TOKEN_STRING_TYPE:
        printf("TOKEN_STRING_TYPE\n");
        break;
    case TOKEN_ARRAY_TYPE:
        printf("TOKEN_ARRAY_TYPE\n");
        break;
    case TOKEN_LIST_TYPE:
        printf("TOKEN_LIST_TYPE\n");
        break;
    default:
        printf("unknown token %d\n", type);
        break;
    }
}

Tokenizer::Tokenizer() {
}

Tokenizer::~Tokenizer() {
}

bool Tokenizer::open_file(Tokenizer* container, const char* path) {
    in_file.open(path, std::ifstream::in);

    if (!in_file.good()) {
        if (container) {
            container->print_err("unable to open %s\n", path);
        } else {
            fprintf(stderr, "error: unable to open %s\n", path);
        }
        return false;
    }

    current_file = path;
    getline(in_file, current_line);
    line_number = 1;
    line_offset = 0;
    memset(peek, 0, sizeof(peek));
    return true;
}

int Tokenizer::get_char() {
    if (line_offset < current_line.length()) {
        return current_line[line_offset++];
    } else if (in_file.eof()) {
        return EOF;
    } else {
        getline(in_file, current_line);
        line_number++;
        line_offset = 0;
        return '\n';
    }
}

int Tokenizer::next_char() {
    if (peek[0]) {
        int ch = peek[0];
        peek[0] = peek[1];
        peek[1] = 0;
        return ch;
    } else {
        return get_char();
    }
}

int Tokenizer::peek_char() {
    if (!peek[0]) {
        peek[0] = next_char();
    }
    return peek[0];
}

void Tokenizer::eat_whitespace() {
    while (1) {
        while (isspace(peek_char())) {
            next_char();
        }
        // handle C style comments
        if (peek_char() == '/') {
            // consume the '/'
            next_char();
            int ch = peek_char();
            if (ch == '/') {
                // read until end of line
                while ((ch = next_char()) != EOF && ch != '\n' && ch != '\r') {}
                if (ch == EOF) {
                    break;
                }
            } else if (ch == '*') {
                next_char();    // consume '*'

                // look for "*/"
                while (1) {
                    while ((ch = next_char()) != EOF && ch != '*') {}
                    if (ch == EOF) {
                        return;
                    }
                    if (peek_char() == '/') {
                        // consume '/'
                        next_char();
                        break;
                    }
                }
            } else {
                // end of whitespace
                // put characters we read into peek
                peek[0] = '/';
                peek[1] = ch;
                return;
            }
        } else {
            break;
        }
    }
}

bool Tokenizer::parse_identifier(Token& token, int ch) {
    std::string string;
    string.append(1, ch);

    ch = peek_char();
    while (isalnum(ch) || ch == '-' || ch == '_') {
        next_char();
        string.append(1, ch);
        ch = peek_char();
    }

    token.type = find_reserved_word(string);
    token.string_value = string;
    return true;
}

bool Tokenizer::parse_integer(Token& token, int ch) {
    int base = 10;
    uint64_t value = 0;

    token.string_value.clear();
    token.string_value.append(1, ch);

    if (ch == '0') {
        base = 8;
        int peek = peek_char();
        if (peek == 'x' || peek == 'X') {
            base = 16;
            next_char();
            ch = next_char();
            token.string_value.append(1, ch);
        }
    }

    // ch now contains highest order digit to parse
    int digit_count = 0;
    while (1) {
        int digit = -1;

        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (base == 16) {
            if (ch >= 'A' && ch <= 'F') {
                digit = ch - 'A' + 10;
            } else if (ch >= 'a' && ch <= 'f') {
                digit = ch - 'a' + 10;
            }
        }

        if (digit < 0) {
            break;
        }

        value = base * value + digit;

        if (++digit_count > 16) {
            print_err("integer value too large\n");
            return false;
        }

        ch = peek_char();
        if (!isdigit(ch) && !(base == 16 &&
                              ((ch >= 'A' && ch <= 'F') ||
                               (ch >= 'a' && ch <= 'f')))) {
            break;
        }
        token.string_value.append(1, ch);
        next_char();
    }

    token.type = TOKEN_INT_LITERAL;
    token.int_value = value;
    return true;
}

bool Tokenizer::parse_string(Token& token) {
    std::string string;
    int ch = next_char();

    while (ch != EOF) {
        if (ch == '\\') {
            ch = next_char();
            if (ch == EOF) {
                break;
            }
            switch (ch) {
            case 'a':
                ch = '\a';
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'v':
                ch = '\v';
                break;
            case '\\':
                ch = '\\';
                break;
            case '\'':
                ch = '\'';
                break;
            case '\"':
                ch = '\"';
                break;
            case '?':
                ch = '?';
                break;
            default:
                print_err("unsupported escape sequence \\%c in string literal\n", ch);
                return false;
            }
        } else if (ch == '\"') {
            token.type = TOKEN_STRING_LITERAL;
            token.string_value = string;
            return true;
        }
        string.append(1, ch);
        ch = next_char();
    }

    print_err("end of file during unterminated string\n");
    return false;
}

    // returns false if we cannot parse the next token
    // EOF is not considered an error
bool Tokenizer::next_token(Token& token) {
    if (have_token_peek) {
        token = token_peek;
        have_token_peek = false;
        return true;
    }

    eat_whitespace();
    int ch = next_char();
    bool result = true;

    if (isalpha(ch)) {
       result = parse_identifier(token, ch);
    } else if (isdigit(ch)) {
        result = parse_integer(token, ch);
    } else if (ch == '\"') {
        result = parse_string(token);
    } else {
        switch (ch) {
        case EOF:
            token.type = TOKEN_EOF;
            break;
        case '{':
            token.type = TOKEN_LIST_START;
            break;
        case '}':
            token.type = TOKEN_LIST_END;
            break;
        case '[':
            token.type = TOKEN_ARRAY_START;
            break;
        case ']':
            token.type = TOKEN_ARRAY_END;
            break;
        case '=':
            token.type = TOKEN_EQUALS;
            break;
        case ',':
            token.type = TOKEN_COMMA;
            break;
        case '.':
            token.type = TOKEN_DOT;
            break;
        case '(':
            token.type = TOKEN_LPAREN;
            break;
        case ')':
            token.type = TOKEN_RPAREN;
            break;
        case '+':
            token.type = TOKEN_PLUS;
            break;
        case '-':
            token.type = TOKEN_MINUS;
            break;
        case '*':
            token.type = TOKEN_TIMES;
            break;
        case '/':
            token.type = TOKEN_DIV;
            break;
        case '%':
            token.type = TOKEN_MOD;
            break;
        case '~':
            token.type = TOKEN_NOT;
            break;
        case '&':
            token.type = TOKEN_AND;
            break;
        case '|':
            token.type = TOKEN_OR;
            break;
        case '^':
            token.type = TOKEN_XOR;
            break;
        case '<':
            if (next_char() == '<') {
                token.type = TOKEN_LSHIFT;
            } else {
                print_err("unexpected token '<'\n");
                result = false;
            }
            break;
        case '>':
            if (next_char() == '>') {
                token.type = TOKEN_RSHIFT;
            } else {
                print_err("unexpected token '>'\n");
                result = false;
            }
            break;
        default:
            print_err("invalid token \'%c\'\n", ch);
            result = false;
        }
        token.string_value.clear();
        token.string_value.append(1, ch);
    }

#if PRINT_TOKENS
    if (result) {
        token.print();
    }
#endif

    return result;
}

bool Tokenizer::peek_token(Token& token) {
    if (!have_token_peek && !next_token(token_peek)) {
        return false;
    }
    token = token_peek;
    have_token_peek = true;
    return true;
}

void Tokenizer::print_err(const char* fmt, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", current_file.c_str(), line_number, line_offset);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
