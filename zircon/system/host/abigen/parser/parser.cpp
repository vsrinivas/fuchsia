// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "parser.h"

using std::string;
using std::vector;

std::vector<string> tokenize_string(const string& str) {
    std::vector<string> tokens;
    string tok;

    for (auto& c : str) {
        if (isalnum(c) || c == '_') {
            tok += c;
        } else {
            if (!tok.empty())
                tokens.push_back(tok);
            tok.clear();
            if (ispunct(c))
                tokens.emplace_back(1, c);
        }
    }
    if (!tok.empty())
        tokens.push_back(tok);

    return tokens;
}

std::vector<string>& operator+=(std::vector<string>& v1, const std::vector<string>& v2) {
    v1.insert(v1.end(), v2.begin(), v2.end());
    return v1;
}

void FileCtx::print_error(const char* what, const string& extra) const {
    if (line_end) {
        fprintf(stderr, "error: %s : lines %d-%d : %s '%s' [near: %s]\n",
                file, line_start, line_end, what, extra.c_str(), last_token);
    } else {
        fprintf(stderr, "error: %s : line %d : %s '%s' [near: %s]\n",
                file, line_start, what, extra.c_str(), last_token);
    }
}

void FileCtx::print_info(const char* what) const {
    fprintf(stderr, "%s : line %d : %s\n", file, line_start, what);
}

std::string eof_str;

const string& TokenStream::curr() {
    if (ix_ >= tokens_.size())
        return eof_str;
    return tokens_[ix_];
}

const string& TokenStream::next() {
    ix_ += 1u;
    if (ix_ >= tokens_.size()) {
        fc_.print_error("unexpected end of file", "");
        return eof_str;
    }
    return tokens_[ix_];
}

const string& TokenStream::peek_next() const {
    auto n = ix_ + 1;
    return (n >= tokens_.size()) ? eof_str : tokens_[n];
}

const FileCtx& TokenStream::filectx() {
    fc_.last_token = curr().c_str();
    return fc_;
}
