# This directory contains the output of bindgen-generated ICU bindings.

## run_bindgen.sh

This script is used to run bindgen manually, or out of band of the normal build
cycle of `rust_icu_sys`.  This is useful for building `rust_icu` without the
`bindgen` feature on; which means that `bindgen` is not run during build, but
a pre-existing bindings file is used instead.

Of course, for that to work, there has to be something that generates the
one-off files.  `run_bindgen.sh` is that something.

## I/O behavior

The input to the script are the headers from the Unicode directory.  The list
of headers to examine is listed in the script itself, and you will find things
like `ucal`, `udat`, and others.  The directory is auto-detected based on the
data that the program `icu-config` gives about the installation.

The output of the script is a rust library file containing the auto-generated
low-level bindings for the ICU library.  The name of the output file depends
on the

## Dependencies

The script attempts to auto-detect its dependencies and will fail early if
one is not detected.  The dependencies known so far are:

- bash
- icu-config (from ICU installations)
- bindgen (from rust tools)
- llvm-config (from the "llvm-dev" package)
- tr (from coreutils)

## Running the script.

The script is intended to be auto-piloted.  Ideally it is invoked from a
Makefile target.  For the time being two things are important here:

1. The list of headers and identifiers that need processing is set separately
   from `build.rs` but shoudl be maintained to keep in sync.

2. Output directory is by default the current directory.  It can be modified by
   setting the environment variable `$OUTPUT_DIR` when starting the program.

