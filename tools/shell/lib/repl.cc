// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "repl.h"

#include <stdio.h>
#include <string.h>

#include <cctype>
#include <set>
#include <string>

namespace shell::repl {

Repl::Repl(JSContext* ctx, const std::string& prompt) 
    : li_([this](const std::string& s) { HandleLine(s); }, prompt), 
      ctx_(ctx), 
      output_fd_(stdout),
      exit_shell_cmd_(false) {
  li_.Show();
}

void Repl::Write(const char* output) { fprintf(output_fd_, "%s", output); }

bool Repl::FeedInput(uint8_t* bytes, size_t num_bytes) {
  bool exit_shell = false;
  for (size_t i = 0; i < num_bytes; i++) {
    li_.OnInput((char)bytes[i]);
    if (exit_shell) {
      return true;
    }
  }
  return false;
}

void Repl::HandleLine(const std::string& line) {
  li_.Hide();
  std::string cmd = mexpr_ + line;
  std::string shell_cmd = GetAndExecuteShellCmd(cmd);
  if (shell_cmd == "\\q") {
    exit_shell_cmd_ =  true;
  }
  else if (shell_cmd != "") {
    mexpr_ = "";
    exit_shell_cmd_ = false;
  }
  else{
    std::string open_symbols = OpenSymbols(cmd);
    if (open_symbols.empty()) {
      mexpr_ = "";
      const char* output = EvalCmd(cmd);
      if (output) {
        Write(output);
        Write("\n");
      }
    } else {
      mexpr_ = cmd;
    }
    exit_shell_cmd_ = false;
  }
  li_.Show();
}

std::string Repl::GetAndExecuteShellCmd(std::string cmd) {
  if (cmd.substr(0, 2) == "\\h") {
    Write("\\q\texit\n\\h\tthis help\n");
    return cmd.substr(0, 2);
  }
  if (cmd.substr(0, 2) == "\\q") {
    return cmd.substr(0, 2);
  }
  return "";
}

const char* Repl::EvalCmd(std::string& cmd) {
  JSValue result = JS_Eval(ctx_, cmd.c_str(), cmd.length(), "<evalScript>", JS_EVAL_TYPE_GLOBAL);
  const char* output = JS_ToCString(ctx_, result);
  if (JS_IsException(result)) {
    js_std_dump_error(ctx_);
    return nullptr;
  }
  return output;
}

bool match(char a, char b) {
  if (a == '(' && b == ')') {
    return true;
  }
  if (a == '{' && b == '}') {
    return true;
  }
  if (a == '[' && b == ']')
    return true;
  return false;
}

bool isIdentifierAllowed(char c) {
  return std::isalpha(c) || std::isdigit(c) || c == '_' || c == '$';
}

std::string Repl::OpenSymbols(std::string& cmd) {
  std::string open_symbols;
  bool regex_possible = true;
  size_t n = cmd.length();
  size_t i = 0;
  while (i < n) {
    if (cmd[i] == '\'' || cmd[i] == '\"' || cmd[i] == '`') {  // string
      char delim = cmd[i];
      open_symbols.push_back(delim);
      i++;
      while (i < n) {
        if (cmd[i] == '\\')
          i++;
        else if (cmd[i] == delim) {
          open_symbols.pop_back();
          i++;
          break;
        }
        i++;
      }
      regex_possible = false;
    }

    else if (cmd[i] == '/') {
      if (cmd[i + 1] == '*') {  // block comment
        std::size_t ip = cmd.find("*/", i + 1);
        if (ip != std::string::npos)
          i = ip + 1;
        else {
          open_symbols.push_back('*');
          i = n;
        }
      } else if (cmd[i + 1] == '/') {  // line comment
        for (i += 2; i < n; i++) {
          if (cmd[i] == '\n')
            break;
        }
        i++;
      } else if (regex_possible) {  // regex
        open_symbols.push_back('/');
        for (i++; i < n; i++) {
          if (cmd[i] == '\\') {
            if (i < n)
              i++;
          } else if (open_symbols.back() == '[') {  // ignore / or [ if within []
            if (cmd[i] == ']')
              open_symbols.pop_back();
          } else if (cmd[i] == '[') {
            open_symbols.push_back('[');
            if (cmd[i] == '[' || cmd[i] == ']')
              i++;
          } else if (cmd[i] == '/') {
            open_symbols.pop_back();
            break;
          }
        }
        i++;
        regex_possible = false;
      } else {
        regex_possible = true;
        i++;
      }
    }

    else if (strchr("{[(", cmd[i])) {
      open_symbols.push_back(cmd[i]);
      regex_possible = true;
      i++;
    }

    else if (strchr("}])", cmd[i])) {
      regex_possible = false;
      if (match(open_symbols.back(), cmd[i]) && !open_symbols.empty()) {
        open_symbols.pop_back();
      }
      i++;
    }

    else if (std::isspace(cmd[i])) {
      i++;
    }

    else if (strchr("+-", cmd[i])) {
      regex_possible = true;
      i++;
    }

    else if (std::isdigit(cmd[i])) {  // a number
      while (i < n && (std::isalnum(cmd[i]) || cmd[i] == '.' || cmd[i] == '+' || cmd[i] == '-')) {
        i++;
      }
    }

    else if (isIdentifierAllowed(cmd[i])) {  // an identifier
      regex_possible = true;
      int old_i = i;
      i++;
      while (i < n && isIdentifierAllowed(cmd[i])) {
        i++;
      }
      std::set<std::string> no_regex_keywords = {
          "this", "super", "undefined", "null", "true", "false", "Infinity", "NaN", "arguments"};
      std::set<std::string> keywords = {
          "break",      "case",      "catch",    "continue",   "debugger",  "default",   "delete",
          "do",         "else",      "finally",  "for",        "function",  "if",        "in",
          "instanceof", "new",       "return",   "switch",     "this",      "throw",     "try",
          "typeof",     "while",     "with",     "class",      "const",     "enum",      "import",
          "export",     "extends",   "super",    "implements", "interface", "let",       "package",
          "private",    "protected", "public",   "static",     "yield",     "undefined", "null",
          "true",       "false",     "Infinity", "NaN",        "eval",      "arguments", "await",
          "void",       "var"};
      if (keywords.count(cmd.substr(old_i, i - old_i)) > 0) {
        if (no_regex_keywords.count(cmd.substr(old_i, i - old_i)) > 0) {
          regex_possible = false;
        }
        continue;
      }
      size_t ip = i;
      while (ip < n && std::isspace(cmd[ip])) {
        ip++;
      }
      if (ip < n && cmd[ip] == '(') {  // beginning of function, regex possible
        continue;
      }
      regex_possible = false;
    } else {
      regex_possible = true;
      i++;
    }
  }
  return open_symbols;
}

}  // namespace shell::repl
