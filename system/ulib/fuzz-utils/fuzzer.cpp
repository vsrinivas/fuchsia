// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <stdio.h>

#include <fbl/string.h>
#include <fuzz-utils/fuzzer.h>
#include <zircon/types.h>

namespace fuzzing {

// Public methods

Fuzzer::~Fuzzer() {}

// Protected methods

Fuzzer::Fuzzer() : out_(stdout), err_(stderr) {}

void Fuzzer::Reset() {
    options_.clear();
    out_ = stdout;
    err_ = stderr;
}

zx_status_t Fuzzer::SetOption(const char* option) {
    ZX_DEBUG_ASSERT(option);

    const char* ptr = option;
    while (*ptr && *ptr != '#' && (*ptr == '-' || isspace(*ptr))) {
        ++ptr;
    }
    const char* mark = ptr;
    while (*ptr && *ptr != '#' && *ptr != '=' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String key(mark, ptr - mark);
    while (*ptr && *ptr != '#' && (*ptr == '=' || isspace(*ptr))) {
        ++ptr;
    }
    mark = ptr;
    while (*ptr && *ptr != '#' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String val(mark, ptr - mark);

    return SetOption(key.c_str(), val.c_str());
}

zx_status_t Fuzzer::SetOption(const char* key, const char* value) {
    ZX_DEBUG_ASSERT(key);
    ZX_DEBUG_ASSERT(value);

    // Ignore blank options
    if (*key == '\0' && *value == '\0') {
        return ZX_OK;
    }

    // Must have both key and value
    if (*key == '\0' || *value == '\0') {
        fprintf(err_, "Empty key or value: '%s'='%s'\n", key, value);
        return ZX_ERR_INVALID_ARGS;
    }

    // Save the option
    options_.set(key, value);

    return ZX_OK;
}

} // namespace fuzzing
