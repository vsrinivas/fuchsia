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

    // Execute the 'fuzz' tool with the given command line arguments.  See also uapp/fuzz/main.cpp.
    static zx_status_t Main(int argc, char** argv);

protected:
    Fuzzer();

    // Resets the object to a pristine state; useful during unit testing
    virtual void Reset();

    // These getters and setters are provided strictly for unit testing purposes.
    const StringMap& options() const { return options_; }
    void set_root(const fbl::String& root) { root_.Set(root); }
    void set_out(FILE* out) { out_ = out; }
    void set_err(FILE* err) { err_ = err; }
    void set_target(const fbl::String& target) { target_.Set(target); }

    // Interpret the given |args| and execute the appropriate subcommand.
    zx_status_t Run(StringList* args);

    // Parses |option| as a key-value pair.  If an option with the same key is already set, it is
    // replaced.  Otherwise, the option is added.  Options are of the form '[-]key=value[#comment]'.
    zx_status_t SetOption(const fbl::String& option);

    // Parses an option made up of a |key|-|value| pair.  If an option with the same key is already
    // set, it is replaced.  Otherwise, the option is added.
    zx_status_t SetOption(const fbl::String& key, const fbl::String& value);

    // Constructs a |Path| object to the |path| directory, relative to the root if set.
    zx_status_t RebasePath(const fbl::String& package, Path* out);

    // Constructs a |Path| object to the |package|'s max version directory.  On error, |out| will be
    // reset to the root directory.
    zx_status_t GetPackagePath(const fbl::String& package, Path* out);

    // Returns a map of names of available fuzzers to executables in |out| belonging to the
    // 'zircon_fuzzers' fuzz package located under |zircon_path| and matching |target|, if
    // specified.
    void FindZirconFuzzers(const fbl::String& zircon_path, const fbl::String& target,
                           StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| belonging to the given
    // fuzz |package| located under "pkgfs/packages" and matching |target|, if specified.
    void FindFuchsiaFuzzers(const fbl::String& package, const fbl::String& target, StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| matching the given
    // |package| and |target|.
    void FindFuzzers(const fbl::String& package, const fbl::String& target, StringMap* out);

    // Returns a map of names of available fuzzers to executables in |out| matching |name|, if
    // specified.
    void FindFuzzers(const fbl::String& name, StringMap* out);

    // Fills in |args| with the arguments for the fuzzer subprocess as currently configured.
    void GetArgs(StringList* args);

    // Spawns a fuzzer sub-process.
    virtual zx_status_t Execute();

    // Callback used by |Walker| to match the fuzz target sub-process and print information on it,
    // or kill it if the |kill| parameter is true.
    friend class Walker;
    bool CheckProcess(zx_handle_t process, bool kill = false) const;

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
    zx_status_t Seeds();
    zx_status_t Start();
    zx_status_t Check();
    zx_status_t Stop();
    zx_status_t Repro();
    zx_status_t Merge();

    // The current subcommand
    uint32_t cmd_;
    // Fuzzer name; may be a user-supplied pattern until resolved into a package/target.
    fbl::String name_;
    // Fuchsia package URL for the fuzzing component.
    fbl::String url_;
    // Fuchsia package name; matches a `fuzz_package` as defined in //build/fuzzing/fuzzer.gni
    fbl::String package_;
    // Fuchsia component name; matches a `fuzz_target` as defined in //build/fuzzing/fuzzer.gni
    fbl::String target_;
    // Path that the resource and data paths are relative to; primarily used for testing.
    fbl::String root_;
    // Path on target to where immutable fuzzing resources are stored
    Path resource_path_;
    // Path on target to where mutable fuzzing inputs and outputs are stored
    Path data_path_;
    // Positional arguments to libFuzzers
    StringList inputs_;
    // libFuzzer option flags
    StringMap options_;
    // The libFuzzer subprocess handle
    zx::process process_;
    // Output file descriptor; primarily used for testing.
    FILE* out_;
    // Error file descriptor; primarily used for testing.
    FILE* err_;
};

} // namespace fuzzing
