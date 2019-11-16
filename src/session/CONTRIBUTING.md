# Session Framework Contributor Guide

This document outlines workflow the tips and tricks from Session Framework
contributors.

## Getting Started

Follow [fuchsia.dev](https://fuchsia.dev)'s [Getting Started](https://fuchsia.dev/fuchsia-src/getting_started.md)
and [Developer Workflow](https://fuchsia.dev/fuchsia-src/development)
instructions to get your development environment set up.

## Source Code Layout

The layout of the `//src/session` directory follows the [Fuchsia Source Code
Layout](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/source_code/layout.md#).

The `session_manager` code lives in `//src/session/bin/session_manager`. High
level descriptions of the contents in the session subdirectories are as follows:

  - `examples`: example session implementations, where each example demonstrates
  a set of related functionality (e.g., a graphical session, a session which
  instantiates an element proposer)
  - `fidl`: internal FIDL definitions
  - `tools`: tools which interact with the `session_manager` and
  running `session`
  - `lib`: libraries which support the development of sessions
  - `tests`: integration tests for `tools` and `bin`

## Tips & Tricks

### `fx set`

Run the following command to build all libraries, binaries, and tests:

```
fx set core.x64 --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```

Note: use `--with` for the tests, otherwise each `run-test` invocation will
trigger an OTA.

### `fx run-test`

Run the following command to build and execute the tests for a given area:

```
fx run-test <test>
```

To find the name to substitute for `<test>`:

  1. Find the relevant `BUILD.gn` file.
  2. Copy the name of the desired `unittest_package` rule.

### Rust IDE Integration

See the [Rust Editor Configuration](https://fuchsia.dev/fuchsia-src/development/languages/rust/editors)
page for general Rust IDE setup instructions.

To provide functionality like go-to-definition and autocomplete most IDE
integrations require a `Cargo.toml` for your project. The session framework
codebase contains many projects, and it's tedious to generate each `Cargo.toml`
file manually.

To generate `Cargo.toml` files for all the session framework projects, run:

```
# Find and build all the Rust targets.
$ fx build $(ag -G 'BUILD\.gn' 'rustc_(library|binary)\(' src/session/ | sed 's/\(.*\)\/BUILD\.gn:[0-9]\+:rustc_\(library\|binary\)("\([a-z_]\+\)") {/\1:\3/g')

# Find all the Rust targets and gen-cargo for each one.
$ for TARGET in $(ag -G 'BUILD\.gn' 'rustc_(library|binary)\(' src/session/ | sed 's/\(.*\)\/BUILD\.gn:[0-9]\+:rustc_\(library\|binary\)("\([a-z_]\+\)") {/\1:\3/g'); do fx gen-cargo $TARGET; done
```

### Chaining Changes

Each `git commit` is uploaded as a separate change to [Gerrit](https://fuchsia-review.googlesource.com/).
`git rebase` makes it easier to split changes up into separate commits and
still edit intermediate commits.

Consider the following scenario:

  1. A developer creates a feature branch and makes commits (e.g., 3 changes).
  2. The developer uploads the commits on the branch for review.
  3. The reviewers leave comments on the first two changes.

The author can then use `git rebase -i HEAD~3` to select the commits they want
to edit. The author can then, for each commit:

  1. Edit the code.
  2. Make sure the code still builds and tests pass without later commits (since
   they are submitted individually).
  3. Run `git rebase --continue` to move to the next change to edit.

Once all the commits have been edited, the author then re-uploads the changes.

### Resolving Merge Conflicts

When resolving merge conflicts it's often useful to rebase a change on
`origin/master` instead of `JIRI_HEAD` (which is what is checked out when
running `jiri update`). `JIRI_HEAD` only updates as changes roll through
global integration, whereas `origin/master` contains all submitted changes.



