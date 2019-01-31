// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace-test-utils/squelch.h"

#include <fbl/string_buffer.h>

namespace trace_testing {

std::unique_ptr<Squelcher> Squelcher::Create(const char* regex_str) {
    // We don't make any assumptions about the copyability of |regex_t|.
    // Therefore we construct it in place.
    auto squelcher = new Squelcher;
    if (regcomp(&squelcher->regex_, regex_str, REG_EXTENDED | REG_NEWLINE) != 0) {
        return nullptr;
    }
    return std::unique_ptr<Squelcher>(squelcher);
}

Squelcher::~Squelcher() {
    regfree(&regex_);
}

fbl::String Squelcher::Squelch(const char* str) {
    fbl::StringBuffer<1024u> buf;
    const char* cur = str;
    const char* end = str + strlen(str);

    while (*cur) {
        // size must be 1 + number of parenthesized subexpressions
        size_t match_count = regex_.re_nsub + 1;
        regmatch_t match[match_count];
        if (regexec(&regex_, cur, match_count, match, 0) != 0) {
            buf.Append(cur, end - cur);
            break;
        }
        size_t offset = 0u;
        for (size_t i = 1; i < match_count; i++) {
            if (match[i].rm_so == -1)
                continue;
            buf.Append(cur, match[i].rm_so - offset);
            buf.Append("<>");
            cur += match[i].rm_eo - offset;
            offset = match[i].rm_eo;
        }
    }

    return buf;
}

}  // namespace trace_testing
