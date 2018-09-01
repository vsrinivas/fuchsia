// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/macros.h>
#include <fbl/string.h>
#include <fuzz-utils/path.h>
#include <fuzz-utils/string-list.h>
#include <fuzz-utils/string-map.h>
#include <lib/zx/process.h>
#include <zircon/types.h>

namespace fuzzing {

// |fuzzing::Fuzzer| is a tool that handles the Zircon and/or Fuchsia conventions for fuzzing
// executables and data when using libFuzzer.  This allows users to get the correct options and
// pages with minimal effort.
//
// This class is designed to make the tool as unit-testable as possible, and the internal methods
// have protected visibility to allow testing.  See uapp/fuzz for the thin |main| wrapper around
// this code.
//
// This approach is expected to break at some point in the future!! Without speculating too much on
// the future, it is expected that running shell commands will get harder and harder, whole
// filesystem views like the one used by this tool will become impossible, and the layout of pkgfs
// change.  Nonetheless, this tool is useful as it enables easier fuzzing today, and provides a
// starting point to iterate towards a "fuzzing service" that more closely adheres to the Fuchsia
// model, even when running Zircon standalone.
class Fuzzer {
public:
    virtual ~Fuzzer();

protected:
    Fuzzer();

    // Resets the object to a pristine state; useful during unit testing
    virtual void Reset();

    // These getters and setters are provided strictly for unit testing purposes.
    const StringMap& options() const { return options_; }
    void set_out(FILE* out) { out_ = out; }
    void set_err(FILE* err) { err_ = err; }

    // Parses |option| as a key-value pair.  If an option with the same key is already set, it is
    // replaced.  Otherwise, the option is added.  Options are of the form '[-]key=value[#comment]'.
    zx_status_t SetOption(const char* option);

    // Parses an option made up of a |key|-|value| pair.  If an option with the same key is already
    // set, it is replaced.  Otherwise, the option is added.
    zx_status_t SetOption(const char* key, const char* value);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Fuzzer);

    // libFuzzer option flags
    StringMap options_;
    // Output file descriptor; primarily used for testing.
    FILE* out_;
    // Error file descriptor; primarily used for testing.
    FILE* err_;
};

} // namespace fuzzing
