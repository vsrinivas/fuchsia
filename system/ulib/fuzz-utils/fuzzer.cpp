// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <stdio.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fuzz-utils/fuzzer.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace fuzzing {

// Public methods

Fuzzer::~Fuzzer() {}

// Protected methods

Fuzzer::Fuzzer() : out_(stdout), err_(stderr) {}

void Fuzzer::Reset() {
    root_.clear();
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

zx_status_t Fuzzer::RebasePath(const char* path, Path* out) {
    zx_status_t rc;

    out->Reset();
    if (!root_.empty() && (rc = out->Push(root_.c_str())) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", root_.c_str(), zx_status_get_string(rc));
        return rc;
    }
    if ((rc = out->Push(path)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::GetPackagePath(const char* package, Path* out) {
    zx_status_t rc;

    if ((rc = RebasePath("pkgfs/packages", out)) != ZX_OK) {
        return rc;
    }
    auto pop_prefix = fbl::MakeAutoCall([&out]() { out->Pop(); });
    if ((rc = out->Push(package)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", package, zx_status_get_string(rc));
        return rc;
    }
    auto pop_package = fbl::MakeAutoCall([&out]() { out->Pop(); });

    auto versions = out->List();
    long int max = -1;
    const char* max_version = nullptr;
    for (const char* version = versions->first(); version; version = versions->next()) {
        if (version[0] == '\0') {
            continue;
        }
        char* endptr = nullptr;
        long int val = strtol(version, &endptr, 10);
        if (endptr[0] != '\0') {
            continue;
        }
        if (val > max) {
            max = val;
            max_version = version;
        }
    }
    if (!max_version) {
        fprintf(err_, "No versions available for package: %s\n", package);
        return ZX_ERR_NOT_FOUND;
    }

    if ((rc = out->Push(max_version)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", max_version, zx_status_get_string(rc));
        return rc;
    }

    pop_package.cancel();
    pop_prefix.cancel();
    return ZX_OK;
}

} // namespace fuzzing
