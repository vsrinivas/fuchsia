// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/command.h"

namespace cmd {

static const char kWhitespace[] = " \t\r\n";

Command::Command() = default;

Command::~Command() = default;

bool Command::Parse(const std::string& line) {
  args_.clear();
  size_t pos = 0;
  while (pos != std::string::npos) {
    size_t start = line.find_first_not_of(kWhitespace, pos);
    if (start == std::string::npos) {
      return true;
    }
    if (line[start] == '#') {
      return true;
    }
    if (line[start] == '"') {
      std::string token;
      if (!ParseQuotedToken(line, start + 1, &token, &pos)) {
        args_.clear();
        return false;
      }
      args_.push_back(std::move(token));
    } else {
      size_t end = line.find_first_of(kWhitespace, start);
      std::string token = line.substr(start, end - start);
      if (token.find('"') != std::string::npos) {
        args_.clear();
        parse_error_ = "Unexpected quotation in token.";
        return false;
      }
      args_.push_back(std::move(token));
      pos = end;
    }
  }
  return true;
}

bool Command::ParseQuotedToken(const std::string& line, size_t pos, std::string* token,
                               size_t* end) {
  std::vector<char> buffer;
  while (pos < line.size()) {
    char ch = line[pos++];
    switch (ch) {
      case '\\':
        if (pos == line.size()) {
          parse_error_ = "Unterminated escape sequence.";
          return false;
        }
        ch = line[pos++];
        switch (ch) {
          case 't':
            buffer.push_back('\t');
            break;
          case 'n':
            buffer.push_back('\n');
            break;
          case 'r':
            buffer.push_back('\r');
            break;
          case '"':
            buffer.push_back('\"');
            break;
          case '\\':
            buffer.push_back('\\');
            break;
          default:
            parse_error_ = "Unknown escape character: ";
            parse_error_ += ch;
            return false;
        }
        break;
      case '"':
        if (pos < line.size()) {
          ch = line[pos];
          if (strchr(kWhitespace, ch) == nullptr) {
            parse_error_ = "Ending quotation mark did not terminate token."; return false;
          }
        }
        *token = std::string(buffer.data(), buffer.size());
        *end = pos;
        return true;
      default:
        buffer.push_back(ch);
        break;
    }
  }
  parse_error_ = "Unterminated quotation."; return false;
}

}  // namespace cmd
