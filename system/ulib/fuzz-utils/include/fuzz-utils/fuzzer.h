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
    void set_root(const char* root) { root_.Set(root); }
    void set_out(FILE* out) { out_ = out; }
    void set_err(FILE* err) { err_ = err; }
    void set_executable(const char* executable) { executable_.Set(executable); }

    // Interpret the given |args| and execute the appropriate subcommand.
    zx_status_t Run(StringList* args);

    // Parses |option| as a key-value pair.  If an option with the same key is already set, it is
    // replaced.  Otherwise, the option is added.  Options are of the form '[-]key=value[#comment]'.
    zx_status_t SetOption(const char* option);

    // Parses an option made up of a |key|-|value| pair.  If an option with the same key is already
    // set, it is replaced.  Otherwise, the option is added.
    zx_status_t SetOption(const char* key, const char* value);

    // Constructs a |Path| object to the |path| directory, relative to the root if set.
    zx_status_t RebasePath(const char* package, Path* out);

    // Constructs a |Path| object to the |package|'s max version directory.  On error, |out| will be
    // reset to the root directory.
    zx_status_t GetPackagePath(const char* package, Path* out);

    // Returns a map of names of available fuzzers to executables in |out| belonging to the
    // 'zircon_fuzzers' fuzz package located under |zircon_path| and matching |target|, if
    // specified.
    void FindZirconFuzzers(const char* zircon_path, const char* target, StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| belonging to the given
    // fuzz |package| located under "pkgfs/packages" and matching |target|, if specified.
    void FindFuchsiaFuzzers(const char* package, const char* target, StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| matching the given
    // |package| and |target|.
    void FindFuzzers(const char* package, const char* target, StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| matching |name|, if
    // specified.
    void FindFuzzers(const char* name, StringMap* out);

    // Callback used by |Walker| to match the fuzz target sub-process and print information on it.
    friend class Walker;
    bool CheckProcess(zx_handle_t process) const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Fuzzer);

    // Parses the subcommand, saves it, and sets the corresponding default options.
    zx_status_t SetCommand(const char* command);

    // Selects a unique fuzzer based on the given |name|, or returns an error if 0 or 2 or more
    // matches are found.
    virtual zx_status_t SetFuzzer(const char* name);

    // Reads and parses the fuzzer's options file.
    zx_status_t LoadOptions();

    // Specific subcommands; see corresponding usage messages
    zx_status_t Help();
    zx_status_t List();

    // The current subcommand
    uint32_t cmd_;
    // Fuzzer name; may be a user-supplied pattern until resolved into a package/target.
    fbl::String name_;
    // Path on target to the fuzzer binary
    fbl::String executable_;
    // Path that the resource and data paths are relative to; primarily used for testing.
    fbl::String root_;
    // Positional arguments to libFuzzers
    StringList inputs_;
    // libFuzzer option flags
    StringMap options_;
    // Output file descriptor; primarily used for testing.
    FILE* out_;
    // Error file descriptor; primarily used for testing.
    FILE* err_;
};

} // namespace fuzzing
