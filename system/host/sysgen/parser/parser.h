// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>
#include <vector>

std::vector<std::string>& operator+=(std::vector<std::string>& v1,
                                     const std::vector<std::string>& v2);
std::vector<std::string> tokenize_string(const std::string& str);

struct FileCtx {
    const char* file;
    const char* last_token;
    int line_start;
    int line_end;
    bool verbose;

    FileCtx(const char* file, bool verbose)
        : file(file), last_token(nullptr),
          line_start(0), line_end(0),
          verbose(verbose) {}

    FileCtx(const FileCtx& src, int start)
        : file(src.file), last_token(src.last_token),
          line_start(start), line_end(src.line_start),
          verbose(src.verbose) {}

    void print_error(const char* what, const std::string& extra) const;
    void print_info(const char* what) const;
};

class TokenStream {
public:
    TokenStream(const std::vector<std::string>& tokens, const FileCtx& fc)
        : fc_(fc), ix_(0u), tokens_(tokens) {}

    const std::string& curr();
    const std::string& next();
    const std::string& peek_next() const;
    const FileCtx& filectx();

private:
    FileCtx fc_;
    size_t ix_;
    const std::vector<std::string>& tokens_;
};

// ======================= generic parsing machinery =============================================
template <typename P>
using ProcFn = bool (*)(P* parser, TokenStream& ts);

template <typename P>
struct Dispatch {
    const char* first_token;
    const char* last_token;
    ProcFn<P> fn;
};

template <typename P>
bool process_line(P* parser, const Dispatch<P>* table,
                  const std::vector<std::string>& tokens,
                  const FileCtx& fc) {
    static std::vector<std::string> acc;
    static int start = 0;

    auto& first = acc.empty() ? tokens[0] : acc[0];
    auto& last = tokens.back();

    start = acc.empty() ? fc.line_start : start;

    size_t ix = 0;
    while (table[ix].fn) {
        auto& d = table[ix++];
        if (first == d.first_token) {

            TokenStream ts(tokens, fc);
            if (!d.last_token)
                return d.fn(parser, ts);

            if (last == d.last_token) {
                if (acc.empty()) {
                    // single line case.
                    return d.fn(parser, ts);
                } else {
                    // multiline case.
                    std::vector<std::string> t(std::move(acc));
                    t += tokens;
                    TokenStream mts(t, FileCtx(fc, start));
                    return d.fn(parser, mts);
                }
            } else {
                // more input is needed.
                acc += tokens;
                return true;
            }
        }
    }

    if (!acc.empty())
        fc.print_error("missing terminator", tokens[0]);
    else
        fc.print_error("unknown token", tokens[0]);
    return false;
}

template <typename P>
bool run_parser(P* parser, const Dispatch<P>* table, const char* input, bool verbose) {
    std::ifstream infile;
    infile.open(input, std::ifstream::in);

    if (!infile.good()) {
        fprintf(stderr, "error: unable to open %s\n", input);
        return false;
    }

    if (verbose)
        fprintf(stderr, "sysgen: processing file %s\n", input);

    bool error = false;
    FileCtx fc(input, verbose);
    std::string line;

    while (!infile.eof()) {
        getline(infile, line);
        ++fc.line_start;
        auto tokens = tokenize_string(line);
        if (tokens.empty())
            continue;

        if (!process_line(parser, table, tokens, fc)) {
            error = true;
            break;
        }
    }

    if (error) {
        fprintf(stderr, "** stopping at line %d. parsing %s failed.\n", fc.line_start, input);
        return false;
    }

    return true;
}
